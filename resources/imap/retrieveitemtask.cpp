/*
    Copyright (c) 2010 Klarälvdalens Datakonsult AB,
                       a KDAB Group company <info@kdab.com>
    Author: Kevin Ottens <kevin@kdab.com>

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

#include "retrieveitemtask.h"
#include "messagehelper.h"

#include "imapresource_debug.h"

#include <KLocalizedString>

#include <Akonadi/KMime/MessageFlags>
#include <kimap/selectjob.h>
#include <kimap/session.h>

RetrieveItemTask::RetrieveItemTask(const ResourceStateInterface::Ptr &resource, QObject *parent)
    : ResourceTask(CancelIfNoSession, resource, parent), m_session(nullptr), m_uid(0), m_messageReceived(false)
{

}

RetrieveItemTask::~RetrieveItemTask()
{
}

void RetrieveItemTask::doStart(KIMAP::Session *session)
{
    m_session = session;

    const QString mailBox = mailBoxForCollection(item().parentCollection());
    m_uid = item().remoteId().toLongLong();

    if (m_uid == 0) {
        qCWarning(IMAPRESOURCE_LOG) << "Remote id is " << item().remoteId();
        cancelTask(i18n("Remote id is empty or invalid"));
        return;
    }

    if (session->selectedMailBox() != mailBox) {
        KIMAP::SelectJob *select = new KIMAP::SelectJob(m_session);
        select->setMailBox(mailBox);
        connect(select, &KJob::result,
                this, &RetrieveItemTask::onSelectDone);
        select->start();
    } else {
        triggerFetchJob();
    }
}

void RetrieveItemTask::onSelectDone(KJob *job)
{
    if (job->error()) {
        cancelTask(job->errorString());
    } else {
        triggerFetchJob();
    }
}

void RetrieveItemTask::triggerFetchJob()
{
    KIMAP::FetchJob *fetch = new KIMAP::FetchJob(m_session);
    KIMAP::FetchJob::FetchScope scope;
    fetch->setUidBased(true);
    fetch->setSequenceSet(KIMAP::ImapSet(m_uid));
    scope.parts.clear();// = parts.toList();
    scope.mode = KIMAP::FetchJob::FetchScope::Content;
    fetch->setScope(scope);
    connect(fetch, SIGNAL(messagesReceived(QString,
                                           QMap<qint64, qint64>,
                                           QMap<qint64, KIMAP::MessageAttribute>,
                                           QMap<qint64, KIMAP::MessagePtr>)),
            this, SLOT(onMessagesReceived(QString,
                                          QMap<qint64, qint64>,
                                          QMap<qint64, KIMAP::MessageAttribute>,
                                          QMap<qint64, KIMAP::MessagePtr>)));
    //TODO: Handle parts retrieval
    //connect( fetch, SIGNAL(partsReceived(QString,QMap<qint64,qint64>,QMap<qint64,KIMAP::MessageParts>)),
    //         this, SLOT(onPartsReceived(QString,QMap<qint64,qint64>,QMap<qint64,KIMAP::MessageParts>)) );
    connect(fetch, &KJob::result,
            this, &RetrieveItemTask::onContentFetchDone);
    fetch->start();
}

void RetrieveItemTask::onMessagesReceived(const QString &mailBox,
        const QMap<qint64, qint64> &uids,
        const QMap<qint64, KIMAP::MessageAttribute> &attrs,
        const QMap<qint64, KIMAP::MessagePtr> &messages)
{
    Q_UNUSED(mailBox);

    KIMAP::FetchJob *fetch = qobject_cast<KIMAP::FetchJob *>(sender());
    Q_ASSERT(fetch != nullptr);
    Q_ASSERT(uids.size() == 1);
    Q_ASSERT(messages.size() == 1);

    Akonadi::Item i = item();

    qCDebug(IMAPRESOURCE_LOG) << "MESSAGE from Imap server" << item().remoteId();
    Q_ASSERT(item().isValid());

    const qint64 number = uids.cbegin().key();
    bool ok;
    const Akonadi::Item remoteItem = resourceState()->messageHelper()->createItemFromMessage(messages[number], uids[number], 0, attrs.values(number), QList<QByteArray>(), fetch->scope(), ok);
    if (!ok) {
        qCWarning(IMAPRESOURCE_LOG) << "Failed to retrieve message " << uids[number];
        cancelTask(i18n("No message retrieved, failed to read the message."));
        return;
    }
    i.setMimeType(remoteItem.mimeType());
    i.setPayload(remoteItem.payload<KMime::Message::Ptr>());
    foreach (const QByteArray &flag, remoteItem.flags()) {
        i.setFlag(flag);
    }

    qCDebug(IMAPRESOURCE_LOG) << "Has Payload: " << i.hasPayload();

    m_messageReceived = true;
    itemRetrieved(i);
}

void RetrieveItemTask::onContentFetchDone(KJob *job)
{
    if (job->error()) {
        cancelTask(job->errorString());
    } else if (!m_messageReceived) {
        cancelTask(i18n("No message retrieved, server reply was empty."));
    }
}

