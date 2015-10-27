/*
    Copyright (c) 2014 Klarälvdalens Datakonsult AB,
                       a KDAB Group company <info@kdab.com>
    Author: Kevin Krammer <kevin.krammer@kdab.com>

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

#include "kolabchangeitemstagstask.h"
#include "kolabresource_debug.h"
#include "tagchangehelper.h"

#include <AkonadiCore/ItemFetchJob>
#include <AkonadiCore/ItemFetchScope>
#include <AkonadiCore/TagFetchJob>

KolabChangeItemsTagsTask::KolabChangeItemsTagsTask(ResourceStateInterface::Ptr resource, QSharedPointer<TagConverter> tagConverter, QObject *parent)
    : KolabRelationResourceTask(resource, parent)
    , mTagConverter(tagConverter)
{
}

void KolabChangeItemsTagsTask::startRelationTask(KIMAP::Session *session)
{
    mSession = session;

    //It's entierly possible that we don't have an rid yet

    // compile a set of changed tags
    Q_FOREACH (const Akonadi::Tag &tag, resourceState()->addedTags()) {
        mChangedTags.append(tag);
    }
    Q_FOREACH (const Akonadi::Tag &tag, resourceState()->removedTags()) {
        mChangedTags.append(tag);
    }
    qCDebug(KOLABRESOURCE_LOG) << mChangedTags;

    processNextTag();
}

void KolabChangeItemsTagsTask::processNextTag()
{
    if (mChangedTags.isEmpty()) {
        changeProcessed();
        return;
    }

    // "take first"
    const Akonadi::Tag tag = mChangedTags.takeFirst();

    //We have to fetch it again in case it changed since the notification was emitted (which is likely)
    //Otherwise we get an empty remoteid for new tags that were immediately applied on an item
    Akonadi::TagFetchJob *fetch = new Akonadi::TagFetchJob(tag);
    connect(fetch, &KJob::result, this, &KolabChangeItemsTagsTask::onTagFetchDone);
}

void KolabChangeItemsTagsTask::onTagFetchDone(KJob *job)
{
    if (job->error()) {
        qCWarning(KOLABRESOURCE_LOG) << "TagFetch failed: " << job->errorString();
        // TODO: we could continue for the other tags?
        cancelTask(job->errorString());
        return;
    }

    const Akonadi::Tag::List tags = static_cast<Akonadi::TagFetchJob *>(job)->tags();
    if (tags.size() != 1) {
        qCWarning(KOLABRESOURCE_LOG) << "Invalid number of tags retrieved: " << tags.size();
        // TODO: we could continue for the other tags?
        cancelTask(job->errorString());
        return;
    }

    Akonadi::ItemFetchJob *fetch = new Akonadi::ItemFetchJob(tags.first());
    fetch->fetchScope().setCacheOnly(true);
    fetch->fetchScope().setAncestorRetrieval(Akonadi::ItemFetchScope::All);
    fetch->fetchScope().setFetchGid(true);
    fetch->fetchScope().fetchFullPayload(true);
    fetch->setProperty("tag", QVariant::fromValue(tags.first()));
    connect(fetch, &KJob::result, this, &KolabChangeItemsTagsTask::onItemsFetchDone);
}

void KolabChangeItemsTagsTask::onItemsFetchDone(KJob *job)
{
    if (job->error()) {
        qCWarning(KOLABRESOURCE_LOG) << "ItemFetch failed: " << job->errorString();
        // TODO: we could continue for the other tags?
        cancelTask(job->errorString());
        return;
    }

    const Akonadi::Item::List items = static_cast<Akonadi::ItemFetchJob *>(job)->items();
    qCDebug(KOLABRESOURCE_LOG) << items.size();

    TagChangeHelper *changeHelper = new TagChangeHelper(this);

    connect(changeHelper, &TagChangeHelper::applyCollectionChanges,
            this, &KolabChangeItemsTagsTask::onApplyCollectionChanged);
    connect(changeHelper, &TagChangeHelper::cancelTask, this, &KolabChangeItemsTagsTask::onCancelTask);
    connect(changeHelper, &TagChangeHelper::changeCommitted, this, &KolabChangeItemsTagsTask::onChangeCommitted);

    const Akonadi::Tag tag = job->property("tag").value<Akonadi::Tag>();
    {
        qCDebug(KOLABRESOURCE_LOG) << "Writing " << tag.name() << " with " << items.size() << " members to the server: ";
        foreach (const Akonadi::Item &item, items) {
            qCDebug(KOLABRESOURCE_LOG) << "member(localid, remoteid): " << item.id() << item.remoteId();
        }
    }
    Q_ASSERT(tag.isValid());
    changeHelper->start(tag, mTagConverter->createMessage(tag, items, resourceState()->userName()), mSession);
}

void KolabChangeItemsTagsTask::onApplyCollectionChanged(const Akonadi::Collection &collection)
{
    mRelationCollection = collection;
    applyCollectionChanges(collection);
}

void KolabChangeItemsTagsTask::onCancelTask(const QString &errorText)
{
    // TODO: we could continue for the other tags?
    cancelTask(errorText);
}

void KolabChangeItemsTagsTask::onChangeCommitted()
{
    processNextTag();
}
