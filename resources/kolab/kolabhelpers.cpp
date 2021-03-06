/*
    Copyright (c) 2014 Christian Mollekopf <mollekopf@kolabsys.com>

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

#include "kolabhelpers.h"
#include "kolabresource_debug.h"
#include <KMime/KMimeMessage>
#include <KCalCore/Incidence>
#include <AkonadiCore/Collection>
#include <AkonadiCore/ItemFetchJob>
#include <AkonadiCore/ItemFetchScope>
#include <akonadi/notes/noteutils.h>
#include <kolabobject.h>
#include <errorhandler.h>
#include <KLocalizedString>
#include <QColor>
#include "tracer.h"


bool KolabHelpers::checkForErrors(const Akonadi::Item &item)
{
    if (!Kolab::ErrorHandler::instance().errorOccured()) {
        Kolab::ErrorHandler::instance().clear();
        return false;
    }

    QString errorMsg;
    foreach (const Kolab::ErrorHandler::Err &error, Kolab::ErrorHandler::instance().getErrors()) {
        errorMsg.append(error.message);
        errorMsg.append(QLatin1String("\n"));
    }

    qCWarning(KOLABRESOURCE_LOG) << "Error on item with id: " << item.id() << " remote id: " << item.remoteId() << ":\n" << errorMsg;
    Kolab::ErrorHandler::instance().clear();
    return true;
}

Akonadi::Item getErrorItem(Kolab::FolderType folderType, const QString &remoteId)
{
    //TODO set title, text and icon
    Akonadi::Item item;
    item.setRemoteId(remoteId);
    switch (folderType) {
    case Kolab::EventType: {
        KCalCore::Event::Ptr event(new KCalCore::Event);
        //FIXME Use message creation date time
        event->setDtStart(KDateTime::currentUtcDateTime());
        event->setSummary(i18n("Corrupt Event"));
        event->setDescription(i18n("Event could not be read. Delete this event to remove it from the server. Technical information: remote identifier %1", remoteId));
        item.setMimeType(KCalCore::Event::eventMimeType());
        item.setPayload(event);
    }
    break;
    case Kolab::TaskType: {
        KCalCore::Todo::Ptr task(new KCalCore::Todo);
        //FIXME Use message creation date time
        task->setDtStart(KDateTime::currentUtcDateTime());
        task->setSummary(i18n("Corrupt Task"));
        task->setDescription(i18n("Task could not be read. Delete this task to remove it from the server."));
        item.setMimeType(KCalCore::Todo::todoMimeType());
        item.setPayload(task);
    }
    break;
    case Kolab::JournalType: {
        KCalCore::Journal::Ptr journal(new KCalCore::Journal);
        //FIXME Use message creation date time
        journal->setDtStart(KDateTime::currentUtcDateTime());
        journal->setSummary(i18n("Corrupt journal"));
        journal->setDescription(i18n("Journal could not be read. Delete this journal to remove it from the server."));
        item.setMimeType(KCalCore::Journal::journalMimeType());
        item.setPayload(journal);
    }
    break;
    case Kolab::ContactType: {
        KContacts::Addressee addressee;
        addressee.setName(i18n("Corrupt Contact"));
        addressee.setNote(i18n("Contact could not be read. Delete this contact to remove it from the server."));
        item.setMimeType(KContacts::Addressee::mimeType());
        item.setPayload(addressee);
    }
    break;
    case Kolab::NoteType: {
        Akonadi::NoteUtils::NoteMessageWrapper note;
        note.setTitle(i18n("Corrupt Note"));
        note.setText(i18n("Note could not be read. Delete this note to remove it from the server."));
        item.setPayload(Akonadi::NoteUtils::noteMimeType());
        item.setPayload(note.message());
    }
    break;
    case Kolab::MailType:
    //We don't convert mails, so that should never fail.
    default:
        qCWarning(KOLABRESOURCE_LOG) << "unhandled folder type: " << folderType;
    }
    return item;
}

Akonadi::Item KolabHelpers::translateFromImap(Kolab::FolderType folderType, const Akonadi::Item &imapItem, bool &ok)
{
    //Avoid trying to convert imap messages
    if (folderType == Kolab::MailType) {
        return imapItem;
    }

    //No payload, probably a flag change or alike, we just pass it through
    if (!imapItem.hasPayload()) {
        return imapItem;
    }
    if (!imapItem.hasPayload<KMime::Message::Ptr>()) {
        qCWarning(KOLABRESOURCE_LOG) << "Payload is not a MessagePtr!";
        Q_ASSERT(false);
        ok = false;
        return imapItem;
    }

    const KMime::Message::Ptr payload = imapItem.payload<KMime::Message::Ptr>();
    const Kolab::KolabObjectReader reader(payload);
    if (checkForErrors(imapItem)) {
        ok = true;
        //We return an error object so the sync keeps working, and we can clean up the mess by simply deleting the object in the application.
        return getErrorItem(folderType, imapItem.remoteId());
    }
    switch (reader.getType()) {
    case Kolab::EventObject:
    case Kolab::TodoObject:
    case Kolab::JournalObject: {
        const KCalCore::Incidence::Ptr incidencePtr = reader.getIncidence();
        if (!incidencePtr) {
            qCWarning(KOLABRESOURCE_LOG) << "Failed to read incidence.";
            ok = false;
            return Akonadi::Item();
        }
        Akonadi::Item newItem(incidencePtr->mimeType());
        newItem.setPayload(incidencePtr);
        newItem.setRemoteId(imapItem.remoteId());
        newItem.setGid(incidencePtr->instanceIdentifier());
        return newItem;
    }
    break;
    case Kolab::NoteObject: {
        const KMime::Message::Ptr note = reader.getNote();
        if (!note) {
            qCWarning(KOLABRESOURCE_LOG) << "Failed to read note.";
            ok = false;
            return Akonadi::Item();
        }
        Akonadi::Item newItem(QStringLiteral("text/x-vnd.akonadi.note"));
        newItem.setPayload(note);
        newItem.setRemoteId(imapItem.remoteId());
        const Akonadi::NoteUtils::NoteMessageWrapper wrapper(note);
        newItem.setGid(wrapper.uid());
        return newItem;
    }
    break;
    case Kolab::ContactObject: {
        Akonadi::Item newItem(KContacts::Addressee::mimeType());
        newItem.setPayload(reader.getContact());
        newItem.setRemoteId(imapItem.remoteId());
        newItem.setGid(reader.getContact().uid());
        return newItem;
    }
    break;
    case Kolab::DistlistObject: {
        KContacts::ContactGroup contactGroup = reader.getDistlist();

        QList<KContacts::ContactGroup::ContactReference> toAdd;
        for (uint index = 0; index < contactGroup.contactReferenceCount(); ++index) {
            const KContacts::ContactGroup::ContactReference &reference = contactGroup.contactReference(index);
            KContacts::ContactGroup::ContactReference ref;
            ref.setGid(reference.uid()); //libkolab set a gid with setUid()
            toAdd << ref;
        }
        contactGroup.removeAllContactReferences();
        for (const KContacts::ContactGroup::ContactReference &ref : qAsConst(toAdd)) {
            contactGroup.append(ref);
        }

        Akonadi::Item newItem(KContacts::ContactGroup::mimeType());
        newItem.setPayload(contactGroup);
        newItem.setRemoteId(imapItem.remoteId());
        newItem.setGid(contactGroup.id());
        return newItem;
    }
    break;
    default:
        qCWarning(KOLABRESOURCE_LOG) << "Object type not handled";
        ok = false;
        break;
    }
    return Akonadi::Item();
}

Akonadi::Item::List KolabHelpers::translateToImap(const Akonadi::Item::List &items, bool &ok)
{
    Akonadi::Item::List imapItems;
    imapItems.reserve(items.count());
    for (const Akonadi::Item &item : items) {
        bool translationOk = true;
        imapItems << translateToImap(item, translationOk);
        if (!translationOk) {
            ok = false;
        }
    }
    return imapItems;
}

static KContacts::ContactGroup convertToGidOnly(const KContacts::ContactGroup &contactGroup)
{
    QList<KContacts::ContactGroup::ContactReference> toAdd;
    for (uint index = 0; index < contactGroup.contactReferenceCount(); ++index) {
        const KContacts::ContactGroup::ContactReference &reference = contactGroup.contactReference(index);
        QString gid;
        if (!reference.gid().isEmpty()) {
            gid = reference.gid();
        } else {
            // WARNING: this is an ugly hack for backwards compatibility. Normally this codepath shouldn't be hit.
            // Replace all references with real data-sets
            // Hopefully all resources are available during saving, so we can look up
            // in the addressbook to get name+email from the UID.

            const Akonadi::Item item(reference.uid().toLongLong());
            Akonadi::ItemFetchJob *job = new Akonadi::ItemFetchJob(item);
            job->fetchScope().fetchFullPayload();
            if (!job->exec()) {
                continue;
            }

            const Akonadi::Item::List items = job->items();
            if (items.count() != 1) {
                continue;
            }
            const KContacts::Addressee addressee = job->items().at(0).payload<KContacts::Addressee>();
            gid = addressee.uid();
        }
        KContacts::ContactGroup::ContactReference ref;
        ref.setUid(gid); //libkolab expects a gid for uid()
        toAdd << ref;
    }
    KContacts::ContactGroup gidOnlyContactGroup = contactGroup;
    gidOnlyContactGroup.removeAllContactReferences();
    for (const KContacts::ContactGroup::ContactReference &ref : qAsConst(toAdd)) {
        gidOnlyContactGroup.append(ref);
    }
    return gidOnlyContactGroup;
}

Akonadi::Item KolabHelpers::translateToImap(const Akonadi::Item &item, bool &ok)
{
    ok = true;
    //imap messages don't need to be translated
    if (item.mimeType() == KMime::Message::mimeType()) {
        Q_ASSERT(item.hasPayload<KMime::Message::Ptr>());
        return item;
    }
    const QLatin1String productId("Akonadi-Kolab-Resource");
    //Everthing stays the same, except mime type and payload
    Akonadi::Item imapItem = item;
    imapItem.setMimeType(QStringLiteral("message/rfc822"));
    try {
        switch (getKolabTypeFromMimeType(item.mimeType())) {
        case Kolab::EventObject:
        case Kolab::TodoObject:
        case Kolab::JournalObject: {
            qCDebug(KOLABRESOURCE_LOG) << "converted event";
            const KMime::Message::Ptr message = Kolab::KolabObjectWriter::writeIncidence(
                                                    item.payload<KCalCore::Incidence::Ptr>(),
                                                    Kolab::KolabV3, productId, QStringLiteral("UTC"));
            imapItem.setPayload(message);
        }
        break;
        case Kolab::NoteObject: {
            qCDebug(KOLABRESOURCE_LOG) << "converted note";
            const KMime::Message::Ptr message = Kolab::KolabObjectWriter::writeNote(
                                                    item.payload<KMime::Message::Ptr>(), Kolab::KolabV3, productId);
            imapItem.setPayload(message);
        }
        break;
        case Kolab::ContactObject: {
            qCDebug(KOLABRESOURCE_LOG) << "converted contact";
            const KMime::Message::Ptr message = Kolab::KolabObjectWriter::writeContact(
                                                    item.payload<KContacts::Addressee>(), Kolab::KolabV3, productId);
            imapItem.setPayload(message);
        }
        break;
        case Kolab::DistlistObject: {
            const KContacts::ContactGroup contactGroup = convertToGidOnly(item.payload<KContacts::ContactGroup>());
            qCDebug(KOLABRESOURCE_LOG) << "converted distlist";
            const KMime::Message::Ptr message = Kolab::KolabObjectWriter::writeDistlist(
                                                    contactGroup, Kolab::KolabV3, productId);
            imapItem.setPayload(message);
        }
        break;
        default:
            qCWarning(KOLABRESOURCE_LOG) << "object type not handled: " << item.id() << item.mimeType();
            ok = false;
            return Akonadi::Item();

        }
    } catch (const Akonadi::PayloadException &e) {
        qCWarning(KOLABRESOURCE_LOG) << "The item contains the wrong or no payload: " << item.id() << item.mimeType();
        qCWarning(KOLABRESOURCE_LOG) << e.what();
        return Akonadi::Item();
    }

    if (checkForErrors(item)) {
        qCWarning(KOLABRESOURCE_LOG) << "an error occurred while trying to translate the item to the kolab format: " << item.id();
        ok = false;
        return Akonadi::Item();
    }
    return imapItem;
}

QByteArray KolabHelpers::kolabTypeForMimeType(const QStringList &contentMimeTypes)
{
    if (contentMimeTypes.contains(KContacts::Addressee::mimeType())) {
        return QByteArrayLiteral("contact");
    } else if (contentMimeTypes.contains(KCalCore::Event::eventMimeType())) {
        return QByteArrayLiteral("event");
    } else if (contentMimeTypes.contains(KCalCore::Todo::todoMimeType())) {
        return QByteArrayLiteral("task");
    } else if (contentMimeTypes.contains(KCalCore::Journal::journalMimeType())) {
        return QByteArrayLiteral("journal");
    } else if (contentMimeTypes.contains(QStringLiteral("application/x-vnd.akonadi.note")) ||
               contentMimeTypes.contains(QStringLiteral("text/x-vnd.akonadi.note"))) {
        return QByteArrayLiteral("note");
    }
    return QByteArray();
}

Kolab::ObjectType KolabHelpers::getKolabTypeFromMimeType(const QString &type)
{
    if (type == KCalCore::Event::eventMimeType()) {
        return Kolab::EventObject;
    } else if (type == KCalCore::Todo::todoMimeType()) {
        return Kolab::TodoObject;
    } else if (type == KCalCore::Journal::journalMimeType()) {
        return Kolab::JournalObject;
    } else if (type == KContacts::Addressee::mimeType()) {
        return Kolab::ContactObject;
    } else if (type == KContacts::ContactGroup::mimeType()) {
        return Kolab::DistlistObject;
    } else if (type == QLatin1String("text/x-vnd.akonadi.note") ||
               type == QLatin1String("application/x-vnd.akonadi.note")) {
        return Kolab::NoteObject;
    }
    return Kolab::InvalidObject;
}

QString KolabHelpers::getMimeType(Kolab::FolderType type)
{
    switch (type) {
    case Kolab::MailType:
        return KMime::Message::mimeType();
    case Kolab::ConfigurationType:
        return QStringLiteral(KOLAB_TYPE_RELATION);
    default:
        qCDebug(KOLABRESOURCE_LOG) << "unhandled folder type: " << type;
    }
    return QString();
}

QStringList KolabHelpers::getContentMimeTypes(Kolab::FolderType type)
{
    QStringList contentTypes;
    contentTypes << Akonadi::Collection::mimeType();
    switch (type) {
    case Kolab::EventType:
        contentTypes <<  KCalCore::Event().mimeType();
        break;
    case Kolab::TaskType:
        contentTypes <<  KCalCore::Todo().mimeType();
        break;
    case Kolab::JournalType:
        contentTypes <<  KCalCore::Journal().mimeType();
        break;
    case Kolab::ContactType:
        contentTypes << KContacts::Addressee::mimeType() << KContacts::ContactGroup::mimeType();
        break;
    case Kolab::NoteType:
        contentTypes << QStringLiteral("text/x-vnd.akonadi.note") << QStringLiteral("application/x-vnd.akonadi.note");
        break;
    case Kolab::MailType:
        contentTypes << KMime::Message::mimeType();
        break;
    case Kolab::ConfigurationType:
        contentTypes << QStringLiteral(KOLAB_TYPE_RELATION);
        break;
    default:
        break;
    }
    return contentTypes;
}

Kolab::FolderType KolabHelpers::folderTypeFromString(const QByteArray &folderTypeName)
{
    const QByteArray stripped = folderTypeName.split('.').first();
    return Kolab::folderTypeFromString(std::string(stripped.data(), stripped.size()));
}

QByteArray KolabHelpers::getFolderTypeAnnotation(const QMap< QByteArray, QByteArray > &annotations)
{
    if (annotations.contains("/shared" KOLAB_FOLDER_TYPE_ANNOTATION)
            && !annotations.value("/shared" KOLAB_FOLDER_TYPE_ANNOTATION).isEmpty()) {
        return annotations.value("/shared" KOLAB_FOLDER_TYPE_ANNOTATION);
    } else if (annotations.contains("/private" KOLAB_FOLDER_TYPE_ANNOTATION)
               && !annotations.value("/private" KOLAB_FOLDER_TYPE_ANNOTATION).isEmpty()) {
        return annotations.value("/private" KOLAB_FOLDER_TYPE_ANNOTATION);
    }
    return annotations.value(KOLAB_FOLDER_TYPE_ANNOTATION);
}

void KolabHelpers::setFolderTypeAnnotation(QMap< QByteArray, QByteArray > &annotations, const QByteArray &value)
{
    annotations["/shared" KOLAB_FOLDER_TYPE_ANNOTATION] = value;
}

QColor KolabHelpers::getFolderColor(const QMap<QByteArray, QByteArray> &annotations)
{
    // kolab saves the color without a "#", so we need to add it to the rgb string to have a propper QColor
    if (annotations.contains("/shared" KOLAB_COLOR_ANNOTATION) && !annotations.value("/shared" KOLAB_COLOR_ANNOTATION).isEmpty()) {
        return QColor(QStringLiteral("#").append(QString::fromUtf8(annotations.value("/shared" KOLAB_COLOR_ANNOTATION))));
    } else if (annotations.contains("/private" KOLAB_COLOR_ANNOTATION) && !annotations.value("/private" KOLAB_COLOR_ANNOTATION).isEmpty()) {
        return QColor(QStringLiteral("#").append(QString::fromUtf8(annotations.value("/private" KOLAB_COLOR_ANNOTATION))));
    }
    return QColor();
}

void KolabHelpers::setFolderColor(QMap<QByteArray, QByteArray> &annotations, const QColor &color)
{
    // kolab saves the color without a "#", so we need to delete the prefix "#" if we save it to the annotations
    annotations["/shared" KOLAB_COLOR_ANNOTATION] = color.name().toAscii().remove(0, 1);
}

QString KolabHelpers::getIcon(Kolab::FolderType type)
{
    switch (type) {
    case Kolab::EventType:
    case Kolab::TaskType:
    case Kolab::JournalType:
        return QStringLiteral("view-calendar");
    case Kolab::ContactType:
        return QStringLiteral("view-pim-contacts");
    case Kolab::NoteType:
        return QStringLiteral("view-pim-notes");
    case Kolab::MailType:
    case Kolab::ConfigurationType:
    case Kolab::FreebusyType:
    case Kolab::FileType:
    case Kolab::LastType:
        return QString();
    }
    return QString();
}

bool KolabHelpers::isHandledType(Kolab::FolderType type)
{
    switch (type) {
    case Kolab::EventType:
    case Kolab::TaskType:
    case Kolab::JournalType:
    case Kolab::ContactType:
    case Kolab::NoteType:
    case Kolab::MailType:
        return true;
    case Kolab::ConfigurationType:
    case Kolab::FreebusyType:
    case Kolab::FileType:
    case Kolab::LastType:
        return false;
    }
    return false;
}

QList<QByteArray> KolabHelpers::ancestorChain(const Akonadi::Collection &col)
{
    Q_ASSERT(col.isValid());
    if (col.parentCollection() == Akonadi::Collection::root() || col == Akonadi::Collection::root() || !col.isValid()) {
        return QList<QByteArray>();
    }
    QList<QByteArray> ancestors = ancestorChain(col.parentCollection());
    Q_ASSERT(!col.remoteId().isEmpty());
    ancestors << col.remoteId().toLatin1().mid(1); //We strip the first character which is always the separator
    return ancestors;
}

QString KolabHelpers::createMemberUrl(const Akonadi::Item &item, const QString &user)
{
    Trace() << item.id() << item.mimeType() << item.gid() << item.hasPayload();
    Kolab::RelationMember member;
    if (item.mimeType() == KMime::Message::mimeType()) {
        if (!item.hasPayload<KMime::Message::Ptr>()) {
            qCWarning(KOLABRESOURCE_LOG) << "Email without payload, failed to add to tag: " << item.id() << item.remoteId();
            return QString();
        }
        KMime::Message::Ptr msg = item.payload<KMime::Message::Ptr>();
        member.uid = item.remoteId().toLong();
        member.user = user;
        member.subject = msg->subject()->asUnicodeString();
        member.messageId = msg->messageID()->asUnicodeString();
        member.date = msg->date()->asUnicodeString();
        member.mailbox = ancestorChain(item.parentCollection());
    } else {
        if (item.gid().isEmpty()) {
            qCWarning(KOLABRESOURCE_LOG) << "Groupware object without GID, failed to add to tag: " << item.id() << item.remoteId();
            return QString();
        }
        member.gid = item.gid();
    }
    return Kolab::generateMemberUrl(member);
}

