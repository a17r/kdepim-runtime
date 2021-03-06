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

#include "storeresultjob.h"

#include <Item>
#include <ItemFetchJob>
#include <ItemModifyJob>
#include <Akonadi/KMime/MessageFlags>
#include "maildispatcher_debug.h"
#include <KLocalizedString>
#include <mailtransportakonadi/errorattribute.h>
#include <mailtransportakonadi/dispatchmodeattribute.h>

using namespace Akonadi;
using namespace MailTransport;

/**
 * @internal
 */
class StoreResultJob::Private
{
public:
    Private(StoreResultJob *qq)
        : q(qq), success(false)
    {
    }

    StoreResultJob *const q;
    Item item;
    bool success;
    QString message;

    // Q_SLOTS:
    void fetchDone(KJob *job);
    void modifyDone(KJob *job);
};

void StoreResultJob::Private::fetchDone(KJob *job)
{
    if (job->error()) {
        return;
    }

    qCDebug(MAILDISPATCHER_LOG);

    const ItemFetchJob *fetchJob = qobject_cast<ItemFetchJob *>(job);
    Q_ASSERT(fetchJob);
    if (fetchJob->items().count() != 1) {
        qCritical() << "Fetched" << fetchJob->items().count() << "items, expected 1.";
        q->setError(Unknown);
        q->setErrorText(i18n("Failed to fetch item."));
        q->commit();
        return;
    }

    // Store result in item.
    Item item = fetchJob->items().at(0);
    if (success) {
        item.clearFlag(Akonadi::MessageFlags::Queued);
        item.setFlag(Akonadi::MessageFlags::Sent);
        item.setFlag(Akonadi::MessageFlags::Seen);
        item.removeAttribute<ErrorAttribute>();
    } else {
        item.setFlag(Akonadi::MessageFlags::HasError);
        ErrorAttribute *errorAttribute = new ErrorAttribute(message);
        item.addAttribute(errorAttribute);

        // If dispatch failed, set the DispatchModeAttribute to Manual.
        // Otherwise, the user will *never* be able to try sending the mail again,
        // as Send Queued Messages will ignore it.
        if (item.hasAttribute<DispatchModeAttribute>()) {
            item.attribute<DispatchModeAttribute>()->setDispatchMode(MailTransport::DispatchModeAttribute::Manual);
        } else {
            item.addAttribute(new DispatchModeAttribute(MailTransport::DispatchModeAttribute::Manual));
        }
    }

    ItemModifyJob *modifyJob = new ItemModifyJob(item, q);
    QObject::connect(modifyJob, SIGNAL(result(KJob*)), q, SLOT(modifyDone(KJob*)));
}

void StoreResultJob::Private::modifyDone(KJob *job)
{
    if (job->error()) {
        return;
    }

    qCDebug(MAILDISPATCHER_LOG);

    q->commit();
}

StoreResultJob::StoreResultJob(const Item &item, bool success, const QString &message, QObject *parent)
    : TransactionSequence(parent),
      d(new Private(this))
{
    d->item = item;
    d->success = success;
    d->message = message;
}

StoreResultJob::~StoreResultJob()
{
    delete d;
}

void StoreResultJob::doStart()
{
    // Fetch item in case it was modified elsewhere.
    ItemFetchJob *job = new ItemFetchJob(d->item, this);
    connect(job, SIGNAL(result(KJob*)), this, SLOT(fetchDone(KJob*)));
}

bool StoreResultJob::success() const
{
    return d->success;
}

QString StoreResultJob::message() const
{
    return d->message;
}

#include "moc_storeresultjob.cpp"
