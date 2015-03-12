/*
    Copyright (c) 2003 Cornelius Schumacher <schumacher@kde.org>
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "birthdaysresource.h"
#include "settings.h"
#include "settingsadaptor.h"
#include "configdialog.h"

#include <collectionfetchjob.h>
#include <itemfetchjob.h>
#include <itemfetchscope.h>
#include <mimetypechecker.h>
#include <monitor.h>
#include <entitydisplayattribute.h>

#include <kcontacts/addressee.h>

#include <KCodecs/KEmailAddress>

#include "birthdays_debug.h"
#include <KLocalizedString>
#include <KWindowSystem>

#include <AkonadiCore/TagCreateJob>

using namespace Akonadi;
using namespace KContacts;
using namespace KCalCore;

BirthdaysResource::BirthdaysResource(const QString &id) :
    ResourceBase(id)
{
    new SettingsAdaptor(Settings::self());
    QDBusConnection::sessionBus().registerObject(QLatin1String("/Settings"),
            Settings::self(), QDBusConnection::ExportAdaptors);

    setName(i18n("Birthdays & Anniversaries"));

    Monitor *monitor = new Monitor(this);
    monitor->setMimeTypeMonitored(Addressee::mimeType());
    monitor->itemFetchScope().fetchFullPayload();
    connect(monitor, &Monitor::itemAdded, this, &BirthdaysResource::contactChanged);
    connect(monitor, &Monitor::itemChanged, this, &BirthdaysResource::contactChanged);
    connect(monitor, &Monitor::itemRemoved, this, &BirthdaysResource::contactRemoved);

    connect(this, &BirthdaysResource::reloadConfiguration, this, &BirthdaysResource::doFullSearch);
}

BirthdaysResource::~BirthdaysResource()
{
}

void BirthdaysResource::configure(WId windowId)
{
    ConfigDialog dlg;
    if (windowId) {
        KWindowSystem::setMainWindow(&dlg, windowId);
    }
    if (dlg.exec()) {
        emit configurationDialogAccepted();
    } else {
        emit configurationDialogRejected();
    }
    doFullSearch();
    synchronizeCollectionTree();
}

void BirthdaysResource::retrieveCollections()
{
    Collection c;
    c.setParentCollection(Collection::root());
    c.setRemoteId(QLatin1String("akonadi_birthdays_resource"));
    c.setName(name());
    c.setContentMimeTypes(QStringList() << QLatin1String("application/x-vnd.akonadi.calendar.event"));
    c.setRights(Collection::ReadOnly);

    EntityDisplayAttribute *attribute = c.attribute<EntityDisplayAttribute>(Collection::AddIfMissing);
    attribute->setIconName(QLatin1String("view-calendar-birthday"));

    Collection::List list;
    list << c;
    collectionsRetrieved(list);
}

void BirthdaysResource::retrieveItems(const Akonadi::Collection &collection)
{
    Q_UNUSED(collection);
    itemsRetrievedIncremental(mPendingItems.values(), mDeletedItems.values());
    mPendingItems.clear();
    mDeletedItems.clear();
}

bool BirthdaysResource::retrieveItem(const Akonadi::Item &item, const QSet< QByteArray > &parts)
{
    Q_UNUSED(parts);
    qint64 contactId = item.remoteId().mid(1).toLongLong();
    ItemFetchJob *job = new ItemFetchJob(Item(contactId), this);
    job->fetchScope().fetchFullPayload();
    connect(job, &ItemFetchJob::result, this, &BirthdaysResource::contactRetrieved);
    return true;
}

void BirthdaysResource::contactRetrieved(KJob *job)
{
    ItemFetchJob *fj = static_cast<ItemFetchJob *>(job);
    if (job->error()) {
        emit error(job->errorText());
        cancelTask();
    } else if (fj->items().count() != 1) {
        cancelTask();
    } else {
        KCalCore::Incidence::Ptr ev;
        if (currentItem().remoteId().startsWith(QLatin1Char('b'))) {
            ev = createBirthday(fj->items().first());
        } else if (currentItem().remoteId().startsWith(QLatin1Char('a'))) {
            ev = createAnniversary(fj->items().first());
        }
        if (!ev) {
            cancelTask();
        } else {
            Item i(currentItem());
            i.setPayload<Incidence::Ptr>(ev);
            itemRetrieved(i);
        }
    }
}

void BirthdaysResource::contactChanged(const Akonadi::Item &item)
{
    if (!item.hasPayload<KContacts::Addressee>()) {
        return;
    }

    KContacts::Addressee contact = item.payload<KContacts::Addressee>();

    if (Settings::self()->filterOnCategories()) {
        bool hasCategory = false;
        const QStringList categories = contact.categories();
        foreach (const QString &cat, Settings::self()->filterCategories()) {
            if (categories.contains(cat)) {
                hasCategory = true;
                break;
            }
        }

        if (!hasCategory) {
            return;
        }
    }

    Event::Ptr event = createBirthday(item);
    if (event) {
        addPendingEvent(event, QString::fromLatin1("b%1").arg(item.id()));
    } else {
        Item i(KCalCore::Event::eventMimeType());
        i.setRemoteId(QString::fromLatin1("b%1").arg(item.id()));
        mDeletedItems[ i.remoteId() ] = i;
    }

    event = createAnniversary(item);
    if (event) {
        addPendingEvent(event, QString::fromLatin1("a%1").arg(item.id()));
    } else {
        Item i(KCalCore::Event::eventMimeType());
        i.setRemoteId(QString::fromLatin1("a%1").arg(item.id()));
        mDeletedItems[ i.remoteId() ] = i;
    }
    synchronize();
}

void BirthdaysResource::addPendingEvent(const KCalCore::Event::Ptr &event, const QString &remoteId)
{
    KCalCore::Incidence::Ptr evptr(event);
    Item i(KCalCore::Event::eventMimeType());
    i.setRemoteId(remoteId);
    i.setPayload(evptr);
    mPendingItems[ remoteId ] = i;
}

void BirthdaysResource::contactRemoved(const Akonadi::Item &item)
{
    Item i(KCalCore::Event::eventMimeType());
    i.setRemoteId(QString::fromLatin1("b%1").arg(item.id()));
    mDeletedItems[ i.remoteId() ] = i;
    i.setRemoteId(QString::fromLatin1("a%1").arg(item.id()));
    mDeletedItems[ i.remoteId() ] = i;
    synchronize();
}

void BirthdaysResource::doFullSearch()
{
    CollectionFetchJob *job = new CollectionFetchJob(Collection::root(), CollectionFetchJob::Recursive, this);
    connect(job, &CollectionFetchJob::collectionsReceived, this, &BirthdaysResource::listContacts);
}

void BirthdaysResource::listContacts(const Akonadi::Collection::List &cols)
{
    MimeTypeChecker contactFilter;
    contactFilter.addWantedMimeType(Addressee::mimeType());
    foreach (const Collection &col, cols) {
        if (!contactFilter.isWantedCollection(col)) {
            continue;
        }
        ItemFetchJob *job = new ItemFetchJob(col, this);
        job->fetchScope().fetchFullPayload();
        connect(job, &ItemFetchJob::itemsReceived, this, &BirthdaysResource::createEvents);
    }
}

void BirthdaysResource::createEvents(const Akonadi::Item::List &items)
{
    foreach (const Item &item, items) {
        contactChanged(item);
    }
}

KCalCore::Event::Ptr BirthdaysResource::createBirthday(const Akonadi::Item &contactItem)
{
    if (!contactItem.hasPayload<KContacts::Addressee>()) {
        return KCalCore::Event::Ptr();
    }
    KContacts::Addressee contact = contactItem.payload<KContacts::Addressee>();

    const QString name = contact.realName().isEmpty() ? contact.nickName() : contact.realName();
    if (name.isEmpty()) {
        qCDebug(BIRTHDAYS_LOG) << "contact " << contact.uid() << contactItem.id() << " has no name, skipping.";
        return KCalCore::Event::Ptr();
    }

    const QDate birthdate = contact.birthday().date();
    if (birthdate.isValid()) {
        const QString summary = i18n("%1's birthday", name);

        Event::Ptr ev = createEvent(birthdate);
        ev->setUid(contact.uid() + QLatin1String("_KABC_Birthday"));

        ev->setCustomProperty("KABC", "BIRTHDAY", QLatin1String("YES"));
        ev->setCustomProperty("KABC", "UID-1", contact.uid());
        ev->setCustomProperty("KABC", "NAME-1", name);
        ev->setCustomProperty("KABC", "EMAIL-1", contact.preferredEmail());
        ev->setSummary(summary);

        checkForUnknownCategories(i18n( "Birthday" ), ev);
        return ev;
    }
    return KCalCore::Event::Ptr();
}

KCalCore::Event::Ptr BirthdaysResource::createAnniversary(const Akonadi::Item &contactItem)
{
    if (!contactItem.hasPayload<KContacts::Addressee>()) {
        return KCalCore::Event::Ptr();
    }
    KContacts::Addressee contact = contactItem.payload<KContacts::Addressee>();

    const QString name = contact.realName().isEmpty() ? contact.nickName() : contact.realName();
    if (name.isEmpty()) {
        qCDebug(BIRTHDAYS_LOG) << "contact " << contact.uid() << contactItem.id() << " has no name, skipping.";
        return KCalCore::Event::Ptr();
    }

    const QString anniversary_string = contact.custom(QLatin1String("KADDRESSBOOK"), QLatin1String("X-Anniversary"));
    if (anniversary_string.isEmpty()) {
        return KCalCore::Event::Ptr();
    }
    const QDate anniversary = QDate::fromString(anniversary_string, Qt::ISODate);
    if (anniversary.isValid()) {
        const QString spouseName = contact.custom(QLatin1String("KADDRESSBOOK"), QLatin1String("X-SpousesName"));

        QString summary;
        if (!spouseName.isEmpty()) {
            QString tname, temail;
            KEmailAddress::extractEmailAddressAndName(spouseName, temail, tname);
            tname = KEmailAddress::quoteNameIfNecessary(tname);
            if ((tname[0] == QLatin1Char('"')) && (tname[tname.length() - 1] == QLatin1Char('"'))) {
                tname.remove(0, 1);
                tname.truncate(tname.length() - 1);
            }
            tname.remove(QLatin1Char('\\'));   // remove escape chars
            KContacts::Addressee spouse;
            spouse.setNameFromString(tname);
            QString name_2 = spouse.nickName();
            if (name_2.isEmpty()) {
                name_2 = spouse.realName();
            }
            summary = i18nc("insert names of both spouses",
                            "%1's & %2's anniversary", name, name_2);
        } else {
            summary = i18nc("only one spouse in addressbook, insert the name",
                            "%1's anniversary", name);
        }

        Event::Ptr event = createEvent(anniversary);
        event->setUid(contact.uid() + QLatin1String("_KABC_Anniversary"));
        event->setSummary(summary);

        event->setCustomProperty("KABC", "UID-1", contact.uid());
        event->setCustomProperty("KABC", "NAME-1", name);
        event->setCustomProperty("KABC", "EMAIL-1", contact.fullEmail());
        event->setCustomProperty("KABC", "ANNIVERSARY", QLatin1String("YES"));
        // insert category
        checkForUnknownCategories(i18n( "Anniversary" ), event);
        return event;
    }
    return KCalCore::Event::Ptr();
}

KCalCore::Event::Ptr BirthdaysResource::createEvent(const QDate &date)
{
    Event::Ptr event(new Event());
    event->setDtStart(KDateTime(date, KDateTime::ClockTime));
    event->setDtEnd(KDateTime(date, KDateTime::ClockTime));
    event->setAllDay(true);
    event->setTransparency(Event::Transparent);

    // Set the recurrence
    Recurrence *recurrence = event->recurrence();
    recurrence->setStartDateTime(KDateTime(date, KDateTime::ClockTime));
    recurrence->setYearly(1);
    if (date.month() == 2 && date.day() == 29) {
        recurrence->addYearlyDay(60);
    }

    // Set the alarm
    event->clearAlarms();
    if (Settings::self()->enableAlarm()) {
        Alarm::Ptr alarm = event->newAlarm();
        alarm->setType(Alarm::Display);
        alarm->setText(event->summary());
        alarm->setTime(KDateTime(date, KDateTime::ClockTime));
        // N days before
        alarm->setStartOffset(Duration(-Settings::self()->alarmDays(), Duration::Days));
        alarm->setEnabled(true);
    }

    return event;
}

void BirthdaysResource::checkForUnknownCategories(const QString &categoryToCheck, Event::Ptr &event)
{
    Akonadi::TagCreateJob *tagCreateJob = new Akonadi::TagCreateJob(Akonadi::Tag(categoryToCheck), this);
    tagCreateJob->setMergeIfExisting(true);
    event->setCategories(categoryToCheck);
}


AKONADI_RESOURCE_MAIN(BirthdaysResource)

