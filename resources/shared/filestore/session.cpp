/*  This file is part of the KDE project
    Copyright (C) 2009 Kevin Krammer <kevin.krammer@gmx.at>

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

#include "session_p.h"

#include "collectioncreatejob.h"
#include "collectiondeletejob.h"
#include "collectionfetchjob.h"
#include "collectionmodifyjob.h"
#include "collectionmovejob.h"
#include "itemcreatejob.h"
#include "itemfetchjob.h"
#include "itemmodifyjob.h"
#include "itemmovejob.h"
#include "storecompactjob.h"

using namespace Akonadi;

FileStore::AbstractJobSession::AbstractJobSession(QObject *parent)
    : QObject(parent)
{
}

FileStore::AbstractJobSession::~AbstractJobSession()
{
}

void FileStore::AbstractJobSession::notifyCollectionsReceived(FileStore::Job *job, const Collection::List &collections)
{
    FileStore::CollectionFetchJob *fetchJob = qobject_cast<FileStore::CollectionFetchJob *>(job);
    if (fetchJob != Q_NULLPTR) {
        fetchJob->handleCollectionsReceived(collections);
    }
}

void FileStore::AbstractJobSession::notifyCollectionCreated(FileStore::Job *job, const Collection &collection)
{
    FileStore::CollectionCreateJob *createJob = qobject_cast<FileStore::CollectionCreateJob *>(job);
    if (createJob != Q_NULLPTR) {
        createJob->handleCollectionCreated(collection);
    }
}

void FileStore::AbstractJobSession::notifyCollectionDeleted(FileStore::Job *job, const Collection &collection)
{
    FileStore::CollectionDeleteJob *deleteJob = qobject_cast<FileStore::CollectionDeleteJob *>(job);
    if (deleteJob != Q_NULLPTR) {
        deleteJob->handleCollectionDeleted(collection);
    }
}

void FileStore::AbstractJobSession::notifyCollectionModified(FileStore::Job *job, const Collection &collection)
{
    FileStore::CollectionModifyJob *modifyJob = qobject_cast<FileStore::CollectionModifyJob *>(job);
    if (modifyJob != Q_NULLPTR) {
        modifyJob->handleCollectionModified(collection);
    }
}

void FileStore::AbstractJobSession::notifyCollectionMoved(FileStore::Job *job, const Collection &collection)
{
    FileStore::CollectionMoveJob *moveJob = qobject_cast<FileStore::CollectionMoveJob *>(job);
    if (moveJob != Q_NULLPTR) {
        moveJob->handleCollectionMoved(collection);
    }
}

void FileStore::AbstractJobSession::notifyItemsReceived(FileStore::Job *job, const Item::List &items)
{
    FileStore::ItemFetchJob *fetchJob = qobject_cast<FileStore::ItemFetchJob *>(job);
    if (fetchJob != Q_NULLPTR) {
        fetchJob->handleItemsReceived(items);
    }
}

void FileStore::AbstractJobSession::notifyItemCreated(FileStore::Job *job, const Item &item)
{
    FileStore::ItemCreateJob *createJob = qobject_cast<FileStore::ItemCreateJob *>(job);
    if (createJob != Q_NULLPTR) {
        createJob->handleItemCreated(item);
    }
}

void FileStore::AbstractJobSession::notifyItemModified(FileStore::Job *job, const Item &item)
{
    FileStore::ItemModifyJob *modifyJob = qobject_cast<FileStore::ItemModifyJob *>(job);
    if (modifyJob != Q_NULLPTR) {
        modifyJob->handleItemModified(item);
    }
}

void FileStore::AbstractJobSession::notifyItemMoved(FileStore::Job *job, const Item &item)
{
    FileStore::ItemMoveJob *moveJob = qobject_cast<FileStore::ItemMoveJob *>(job);
    if (moveJob != Q_NULLPTR) {
        moveJob->handleItemMoved(item);
    }
}

void FileStore::AbstractJobSession::notifyCollectionsChanged(FileStore::Job *job, const Collection::List &collections)
{
    FileStore::StoreCompactJob *compactJob = qobject_cast<FileStore::StoreCompactJob *>(job);
    if (compactJob != Q_NULLPTR) {
        compactJob->handleCollectionsChanged(collections);
    }
}

void FileStore::AbstractJobSession::notifyItemsChanged(FileStore::Job *job, const Item::List &items)
{
    FileStore::StoreCompactJob *compactJob = qobject_cast<FileStore::StoreCompactJob *>(job);
    if (compactJob != Q_NULLPTR) {
        compactJob->handleItemsChanged(items);
    }
}

void FileStore::AbstractJobSession::setError(FileStore::Job *job, int errorCode, const QString &errorText)
{
    job->setError(errorCode);
    job->setErrorText(errorText);
}

void FileStore::AbstractJobSession::emitResult(FileStore::Job *job)
{
    removeJob(job);

    job->emitResult();
}

