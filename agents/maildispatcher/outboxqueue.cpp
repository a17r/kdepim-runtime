/*
    Copyright (c) 2009 Constantin Berzan <exit3219@gmail.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#include "outboxqueue.h"

#include <QMultiMap>
#include <QSet>
#include <QTimer>

#include "maildispatcher_debug.h"
#include <KLocalizedString>

#include <Attribute>
#include <ItemFetchJob>
#include <ItemFetchScope>
#include <Monitor>
#include <Akonadi/KMime/AddressAttribute>
#include <Akonadi/KMime/MessageFlags>
#include <Akonadi/KMime/SpecialMailCollections>
#include <Akonadi/KMime/SpecialMailCollectionsRequestJob>

#include <kmime/kmime_message.h>

#include <mailtransportakonadi/dispatchmodeattribute.h>
#include <mailtransportakonadi/sentbehaviourattribute.h>
#include <mailtransportakonadi/transportattribute.h>

using namespace Akonadi;
using namespace MailTransport;

static const int OUTBOX_DISCOVERY_RETRIES = 3; // number of times we try to find or create the outbox
static const int OUTBOX_DISCOVERY_WAIT_TIME = 5000; // number of ms to wait before retrying

/**
 * @internal
 */
class OutboxQueue::Private
{
public:
    Private(OutboxQueue *qq)
        : q(qq),
          outbox(-1),
          monitor(nullptr),
          futureTimer(nullptr),
          totalSize(0),
          outboxDiscoveryRetries(0)
    {
    }

    OutboxQueue *const q;

    Collection outbox;
    Monitor *monitor;
    QList<Item> queue;
    QSet<Item> futureItems; // keeps track of items removed in the meantime
    QMultiMap<QDateTime, Item> futureMap;
    QTimer *futureTimer;
    qulonglong totalSize;
    int outboxDiscoveryRetries;

#if 0
    // If an item is modified externally between the moment we pass it to
    // the MDA and the time the MDA marks it as sent, then we will get
    // itemChanged() and may mistakenly re-add the item to the queue.
    // So we ignore the item that we pass to the MDA, until the MDA finishes
    // sending it.
    Item currentItem;
#endif
    // HACK: The above is not enough.
    // Apparently change notifications are delayed sometimes (???)
    // and we re-add an item long after it was sent.  So keep a list of sent
    // items.
    // TODO debug and figure out why this happens.
    QSet<Item::Id> ignore;

    void initQueue();
    void addIfComplete(const Item &item);

    // Q_SLOTS:
    void checkFuture();
    void collectionFetched(KJob *job);
    void itemFetched(KJob *job);
    void localFoldersChanged();
    void localFoldersRequestResult(KJob *job);
    void itemAdded(const Item &item);
    void itemChanged(const Item &item);
    void itemMoved(const Item &item, const Collection &source, const Collection &dest);
    void itemRemoved(const Item &item);
    void itemProcessed(const Item &item, bool result);
};

void OutboxQueue::Private::initQueue()
{
    totalSize = 0;
    queue.clear();

    qCDebug(MAILDISPATCHER_LOG) << "Fetching items in collection" << outbox.id();
    ItemFetchJob *job = new ItemFetchJob(outbox);
    job->fetchScope().fetchAllAttributes();
    job->fetchScope().fetchFullPayload(false);
    connect(job, SIGNAL(result(KJob*)), q, SLOT(collectionFetched(KJob*)));
}

void OutboxQueue::Private::addIfComplete(const Item &item)
{
    if (ignore.contains(item.id())) {
        qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "is ignored.";
        return;
    }

    if (queue.contains(item)) {
        qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "already in queue!";
        return;
    }

    if (!item.hasAttribute<AddressAttribute>()) {
        qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "does not have the required attribute Address.";
        return;
    }

    if (!item.hasAttribute<DispatchModeAttribute>()) {
        qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "does not have the required attribute DispatchMode.";
        return;
    }

    if (!item.hasAttribute<SentBehaviourAttribute>()) {
        qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "does not have the required attribute SentBehaviour.";
        return;
    }

    if (!item.hasAttribute<TransportAttribute>()) {
        qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "does not have the required attribute Transport.";
        return;
    }

    if (!item.hasFlag(Akonadi::MessageFlags::Queued)) {
        qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "has no '$QUEUED' flag.";
        return;
    }

    const DispatchModeAttribute *dispatchModeAttribute = item.attribute<DispatchModeAttribute>();
    Q_ASSERT(dispatchModeAttribute);
    if (dispatchModeAttribute->dispatchMode() == DispatchModeAttribute::Manual) {
        qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "is queued to be sent manually.";
        return;
    }

    const TransportAttribute *transportAttribute = item.attribute<TransportAttribute>();
    Q_ASSERT(transportAttribute);
    if (transportAttribute->transport() == nullptr) {
        qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "has invalid transport.";
        return;
    }

    const SentBehaviourAttribute *sentBehaviourAttribute = item.attribute<SentBehaviourAttribute>();
    Q_ASSERT(sentBehaviourAttribute);
    if (sentBehaviourAttribute->sentBehaviour() == SentBehaviourAttribute::MoveToCollection &&
            !sentBehaviourAttribute->moveToCollection().isValid()) {
        qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "has invalid sent-mail collection.";
        return;
    }

    // This check requires fetchFullPayload. -> slow (?)
    /*
    if ( !item.hasPayload<KMime::Message::Ptr>() ) {
      qCWarning(MAILDISPATCHER_LOG) << "Item" << item.id() << "does not have KMime::Message::Ptr payload.";
      return;
    }
    */

    if (dispatchModeAttribute->dispatchMode() == DispatchModeAttribute::Automatic &&
            dispatchModeAttribute->sendAfter().isValid() &&
            dispatchModeAttribute->sendAfter() > QDateTime::currentDateTime()) {
        // All the above was OK, so accept it for the future.
        qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "is accepted to be sent in the future.";
        futureMap.insert(dispatchModeAttribute->sendAfter(), item);
        Q_ASSERT(!futureItems.contains(item));
        futureItems.insert(item);
        checkFuture();
        return;
    }

    qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "is accepted into the queue (size" << item.size() << ").";
    Q_ASSERT(!queue.contains(item));
    totalSize += item.size();
    queue.append(item);
    Q_EMIT q->newItems();
}

void OutboxQueue::Private::checkFuture()
{
    qCDebug(MAILDISPATCHER_LOG) << "The future is here." << futureMap.count() << "items in futureMap.";
    Q_ASSERT(futureTimer);
    futureTimer->stop();
    // By default, re-check in one hour.
    futureTimer->setInterval(60 * 60 * 1000);

    // Check items in ascending order of date.
    while (!futureMap.isEmpty()) {
        QMap<QDateTime, Item>::iterator it = futureMap.begin();
        qCDebug(MAILDISPATCHER_LOG) << "Item with due date" << it.key();
        if (it.key() > QDateTime::currentDateTime()) {
            const int secs = QDateTime::currentDateTime().secsTo(it.key()) + 1;
            qCDebug(MAILDISPATCHER_LOG) << "Future, in" << secs << "seconds.";
            Q_ASSERT(secs >= 0);
            if (secs < 60 * 60) {
                futureTimer->setInterval(secs * 1000);
            }
            break; // all others are in the future too
        }
        if (!futureItems.contains(it.value())) {
            qCDebug(MAILDISPATCHER_LOG) << "Item disappeared.";
        } else {
            qCDebug(MAILDISPATCHER_LOG) << "Due date is here. Queuing.";
            addIfComplete(it.value());
            futureItems.remove(it.value());
        }
        futureMap.erase(it);
    }

    qCDebug(MAILDISPATCHER_LOG) << "Timer set to checkFuture again in" << futureTimer->interval() / 1000 << "seconds"
                                << "(that is" << futureTimer->interval() / 1000 / 60 << "minutes).";

    futureTimer->start();
}

void OutboxQueue::Private::collectionFetched(KJob *job)
{
    if (job->error()) {
        qCWarning(MAILDISPATCHER_LOG) << "Failed to fetch outbox collection.  Queue will be empty until the outbox changes.";
        return;
    }

    const ItemFetchJob *fetchJob = qobject_cast<ItemFetchJob *>(job);
    Q_ASSERT(fetchJob);
    qCDebug(MAILDISPATCHER_LOG) << "Fetched" << fetchJob->items().count() << "items.";

    const Akonadi::Item::List lst = fetchJob->items();
    for (const Item &item : lst ) {
        addIfComplete(item);
    }
}

void OutboxQueue::Private::itemFetched(KJob *job)
{
    if (job->error()) {
        qCDebug(MAILDISPATCHER_LOG) << "Error fetching item:" << job->errorString() << ". Trying next item in queue.";
        q->fetchOne();
    }

    const ItemFetchJob *fetchJob = qobject_cast<ItemFetchJob *>(job);
    Q_ASSERT(fetchJob);
    if (fetchJob->items().count() != 1) {
        qCDebug(MAILDISPATCHER_LOG) << "Fetched" << fetchJob->items().count() << ", expected 1. Trying next item in queue.";
        q->fetchOne();
    }

    if (!fetchJob->items().isEmpty()) {
        Q_EMIT q->itemReady(fetchJob->items().at(0));
    }
}

void OutboxQueue::Private::localFoldersChanged()
{
    // Called on startup, and whenever the local folders change.

    if (SpecialMailCollections::self()->hasDefaultCollection(SpecialMailCollections::Outbox)) {
        // Outbox is ready, init the queue from it.
        const Collection collection = SpecialMailCollections::self()->defaultCollection(SpecialMailCollections::Outbox);
        Q_ASSERT(collection.isValid());

        if (outbox != collection) {
            monitor->setCollectionMonitored(outbox, false);
            monitor->setCollectionMonitored(collection, true);
            outbox = collection;
            qCDebug(MAILDISPATCHER_LOG) << "Changed outbox to" << outbox.id();
            initQueue();
        }
    } else {
        // Outbox is not ready. Request it, since otherwise we will not know when
        // new messages appear.
        // (Note that we are a separate process, so we get no notification when
        // MessageQueueJob requests the Outbox.)
        monitor->setCollectionMonitored(outbox, false);
        outbox = Collection(-1);

        SpecialMailCollectionsRequestJob *job = new SpecialMailCollectionsRequestJob(q);
        job->requestDefaultCollection(SpecialMailCollections::Outbox);
        connect(job, SIGNAL(result(KJob*)), q, SLOT(localFoldersRequestResult(KJob*)));

        qCDebug(MAILDISPATCHER_LOG) << "Requesting outbox folder.";
        job->start();
    }

    // make sure we have a place to dump the sent mails as well
    if (!SpecialMailCollections::self()->hasDefaultCollection(SpecialMailCollections::SentMail)) {
        SpecialMailCollectionsRequestJob *job = new SpecialMailCollectionsRequestJob(q);
        job->requestDefaultCollection(SpecialMailCollections::SentMail);

        qCDebug(MAILDISPATCHER_LOG) << "Requesting sent-mail folder";
        job->start();
    }
}

void OutboxQueue::Private::localFoldersRequestResult(KJob *job)
{
    if (job->error()) {
        // We tried to create the outbox, but that failed. This could be because some
        // other process, the mail app, for example, tried to create it at the
        // same time. So try again, once or twice, but wait a little in between, longer
        // each time. If we still haven't managed to create it after a few retries,
        // error hard.

        if (++outboxDiscoveryRetries <= OUTBOX_DISCOVERY_RETRIES) {
            const int timeout = OUTBOX_DISCOVERY_WAIT_TIME * outboxDiscoveryRetries;
            qCWarning(MAILDISPATCHER_LOG) << "Failed to get outbox folder. Retrying in: " << timeout;
            QTimer::singleShot(timeout, q, SLOT(localFoldersChanged()));
        } else {
            qCWarning(MAILDISPATCHER_LOG) << "Failed to get outbox folder. Giving up.";
            Q_EMIT q->error(i18n("Could not access the outbox folder (%1).", job->errorString()));
        }
        return;
    }

    localFoldersChanged();
}

void OutboxQueue::Private::itemAdded(const Item &item)
{
    addIfComplete(item);
}

void OutboxQueue::Private::itemChanged(const Item &item)
{
    addIfComplete(item);
    // TODO: if the item is moved out of the outbox, will I get itemChanged?
}

void OutboxQueue::Private::itemMoved(const Item &item, const Collection &source, const Collection &destination)
{
    if (source == outbox) {
        itemRemoved(item);
    } else if (destination == outbox) {
        addIfComplete(item);
    }
}

void OutboxQueue::Private::itemRemoved(const Item &removedItem)
{
    // @p item has size=0, so get the size from our own copy.
    const int index = queue.indexOf(removedItem);
    if (index == -1) {
        // Item was not in queue at all.
        return;
    }

    Item item(queue.takeAt(index));
    qCDebug(MAILDISPATCHER_LOG) << "Item" << item.id() << "(size" << item.size() << ") was removed from the queue.";
    totalSize -= item.size();

    futureItems.remove(removedItem);
}

void OutboxQueue::Private::itemProcessed(const Item &item, bool result)
{
    Q_ASSERT(ignore.contains(item.id()));
    if (!result) {
        // Give the user a chance to re-send the item if it failed.
        ignore.remove(item.id());
    }
}

OutboxQueue::OutboxQueue(QObject *parent)
    : QObject(parent),
      d(new Private(this))
{
    d->monitor = new Monitor(this);
    d->monitor->itemFetchScope().fetchAllAttributes();
    d->monitor->itemFetchScope().fetchFullPayload(false);
    connect(d->monitor, SIGNAL(itemAdded(Akonadi::Item,Akonadi::Collection)),
            this, SLOT(itemAdded(Akonadi::Item)));
    connect(d->monitor, SIGNAL(itemChanged(Akonadi::Item,QSet<QByteArray>)),
            this, SLOT(itemChanged(Akonadi::Item)));
    connect(d->monitor, SIGNAL(itemMoved(Akonadi::Item,Akonadi::Collection,Akonadi::Collection)),
            this, SLOT(itemMoved(Akonadi::Item,Akonadi::Collection,Akonadi::Collection)));
    connect(d->monitor, SIGNAL(itemRemoved(Akonadi::Item)),
            this, SLOT(itemRemoved(Akonadi::Item)));

    connect(SpecialMailCollections::self(), SIGNAL(defaultCollectionsChanged()), this, SLOT(localFoldersChanged()));
    d->localFoldersChanged();

    d->futureTimer = new QTimer(this);
    connect(d->futureTimer, SIGNAL(timeout()), this, SLOT(checkFuture()));
    d->futureTimer->start(60 * 60 * 1000);   // 1 hour
}

OutboxQueue::~OutboxQueue()
{
    delete d;
}

bool OutboxQueue::isEmpty() const
{
    return d->queue.isEmpty();
}

int OutboxQueue::count() const
{
    const int numberOfElement(d->queue.count());
    if (numberOfElement == 0) {
        // TODO Is this asking for too much?
        Q_ASSERT(d->totalSize == 0);
    }
    return numberOfElement;
}

qulonglong OutboxQueue::totalSize() const
{
    return d->totalSize;
}

void OutboxQueue::fetchOne()
{
    if (isEmpty()) {
        qCDebug(MAILDISPATCHER_LOG) << "Empty queue.";
        return;
    }

    const Item item = d->queue.takeFirst();

    d->totalSize -= item.size();
    Q_ASSERT(!d->ignore.contains(item.id()));
    d->ignore.insert(item.id());

    ItemFetchJob *job = new ItemFetchJob(item);
    job->fetchScope().fetchAllAttributes();
    job->fetchScope().fetchFullPayload();
    connect(job, SIGNAL(result(KJob*)), this, SLOT(itemFetched(KJob*)));
}

#include "moc_outboxqueue.cpp"
