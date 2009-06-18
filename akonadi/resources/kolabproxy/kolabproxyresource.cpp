/*
    Copyright (c) 2009 Andras Mantia <amantia@kde.org>

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

#include "kolabproxyresource.h"

#include "settings.h"
#include "settingsadaptor.h"
#include "collectionannotationsattribute.h"
#include "addressbookhandler.h"
#include "collectiontreebuilder.h"

#include <akonadi/attributefactory.h>
#include <akonadi/cachepolicy.h>
#include <akonadi/collectioncreatejob.h>
#include <akonadi/collectiondeletejob.h>
#include <akonadi/collectionfetchjob.h>
#include <akonadi/itemcreatejob.h>
#include <akonadi/itemdeletejob.h>
#include <akonadi/itemfetchjob.h>
#include <akonadi/itemfetchscope.h>
#include <akonadi/itemmodifyjob.h>
#include <akonadi/monitor.h>
#include <akonadi/item.h>
#include <akonadi/changerecorder.h>
#include <akonadi/entitydisplayattribute.h>
#include <akonadi/session.h>

#include <KLocale>

#include <QtDBus/QDBusConnection>
#include <QSet>
#include <akonadi/collectionmodifyjob.h>

using namespace Akonadi;

KolabProxyResource::KolabProxyResource( const QString &id )
  : ResourceBase( id )
{
  AttributeFactory::registerAttribute<CollectionAnnotationsAttribute>();

  new SettingsAdaptor( Settings::self() );
  QDBusConnection::sessionBus().registerObject( QLatin1String( "/Settings" ),
                            Settings::self(), QDBusConnection::ExportAdaptors );

  changeRecorder()->fetchCollection( true );
  changeRecorder()->itemFetchScope().fetchFullPayload();

  m_monitor = new Monitor( this );
  m_monitor->itemFetchScope().fetchFullPayload();

  m_collectionMonitor = new Monitor( this );
  m_collectionMonitor->fetchCollection( true );
  m_collectionMonitor->setCollectionMonitored(Collection::root());
  m_collectionMonitor->ignoreSession( Session::defaultSession() );

  connect(m_monitor, SIGNAL(itemAdded(const Akonadi::Item & , const Akonadi::Collection &)), this, SLOT(imapItemAdded(const Akonadi::Item & , const Akonadi::Collection &)));
  connect(m_monitor, SIGNAL(itemRemoved(const Akonadi::Item &)), this, SLOT(imapItemRemoved(const Akonadi::Item &)));
  connect(m_collectionMonitor, SIGNAL(collectionAdded(const Akonadi::Collection &, const Akonadi::Collection &)), this, SLOT(imapCollectionAdded(const Akonadi::Collection &, const Akonadi::Collection &)));
  connect(m_collectionMonitor, SIGNAL(collectionRemoved(const Akonadi::Collection &)), this, SLOT(imapCollectionRemoved(const Akonadi::Collection &)));
  connect(m_collectionMonitor, SIGNAL(collectionChanged(const Akonadi::Collection &)), this, SLOT(imapCollectionChanged(const Akonadi::Collection &)));

  m_root.setName( identifier() );
  m_root.setParent( Collection::root() );
  EntityDisplayAttribute *attr = m_root.attribute<EntityDisplayAttribute>( Collection::AddIfMissing );
  attr->setDisplayName( i18n("Kolab") );
  attr->setIconName( "kolab" );
  m_root.setContentMimeTypes( QStringList() << Collection::mimeType() );
  m_root.setRemoteId( identifier() );
  m_root.setRights( Collection::ReadOnly );

  setName( i18n("Kolab") );

  // among other things, this ensures that m_root actually exists when a new imap folder is added
  synchronizeCollectionTree();
}

KolabProxyResource::~KolabProxyResource()
{
}

void KolabProxyResource::retrieveCollections()
{
  kDebug() << "RETRIEVECOLLECTIONS ";
  CollectionTreeBuilder *job = new CollectionTreeBuilder( this );
  connect(job, SIGNAL(result(KJob*)), this, SLOT(retrieveCollectionsTreeDone(KJob *)));
}

void KolabProxyResource::retrieveCollectionsTreeDone(KJob* job)
{
  if ( job->error() ) {
    kWarning( ) << "Error on collection fetch:" << job->errorText();
    cancelTask( job->errorText() );
  } else {
    Collection::List imapCollections = qobject_cast<CollectionTreeBuilder*>( job )->allCollections();

    Collection::List kolabCollections;
    kolabCollections.append( m_root );

    Q_FOREACH(const Collection &collection, imapCollections)
      kolabCollections.append( createCollection(collection) );
    collectionsRetrieved( kolabCollections );
  }

}

void KolabProxyResource::retrieveItems( const Collection &collection )
{
  kDebug() << "RETRIEVEITEMS";
  m_retrieveState = RetrieveItems;
  ItemFetchJob *job = new ItemFetchJob( Collection(collection.remoteId().toUInt()) );
  job->fetchScope().fetchFullPayload();
  connect(job, SIGNAL(result(KJob*)), this, SLOT(retrieveItemFetchDone(KJob *)));
}

bool KolabProxyResource::retrieveItem( const Item &item, const QSet<QByteArray> &parts )
{
  kDebug() << "RETRIEVEITEM";
  m_retrieveState = RetrieveItem;
  ItemFetchJob *job = new ItemFetchJob( item );
  job->fetchScope().fetchFullPayload();
  connect(job, SIGNAL(result(KJob*)), this, SLOT(retrieveItemFetchDone(KJob *)));
  return true;
}

void KolabProxyResource::retrieveItemFetchDone(KJob *job)
{
  if ( job->error() ) {
    kWarning( ) << "Error on item fetch:" << job->errorText();
    cancelTask();
  } else {
    Item::Id collectionId = -1;
    Item::List items = qobject_cast<ItemFetchJob*>(job)->items();
    if (items.size() < 1) {
      kWarning() << "Items is emtpy";
      cancelTask();
      return;
    }
    collectionId = items[0].storageCollectionId();
    KolabHandler *handler = m_monitoredCollections.value(collectionId);
    if (handler) {
      if (m_retrieveState == DeleteItem) {
        kDebug() << "m_retrieveState = DeleteItem";
        handler->itemDeleted(items[0]);
      } else {
        Item::List newItems = handler->translateItems(items);
        if (m_retrieveState == RetrieveItems) {
          itemsRetrieved(newItems);
        } else
          itemRetrieved(newItems[0]);
      }
      kDebug() << "RETRIEVEITEM DONE";
    } else {
      cancelTask();
    }
  }
}

void KolabProxyResource::aboutToQuit()
{
  qDeleteAll(m_monitoredCollections);
}

void KolabProxyResource::configure( WId windowId )
{
  Q_UNUSED( windowId );

  // TODO: this method is usually called when a new resource is being
  // added to the Akonadi setup. You can do any kind of user interaction here,
  // e.g. showing dialogs.
  // The given window ID is usually useful to get the correct
  // "on top of parent" behavior if the running window manager applies any kind
  // of focus stealing prevention technique
}

void KolabProxyResource::itemAdded( const Item &item, const Collection &collection )
{
  kDebug() << "ITEMADDED";

  Item kolabItem( item );
//   kDebug() << "Item added " << item.id() << collection.remoteId() << collection.id();

  Collection imapCollection( collection.remoteId().toUInt() );
  CollectionFetchJob* coljob = new CollectionFetchJob( Collection::List() << imapCollection );
  if ( coljob->exec() ) {
    Collection::List collections = coljob->collections();
    imapCollection = collections[0];
  }else {
    kWarning() << "Can't fetch collection" << imapCollection.id();
  }

  KolabHandler *handler  = m_monitoredCollections.value(imapCollection.id());
  if (!handler) {
    kWarning() << "No handler found";
    cancelTask();
    return;
  }
  Item imapItem(handler->contentMimeTypes()[0]);
  handler->toKolabFormat( kolabItem, imapItem );

  ItemCreateJob *cjob = new ItemCreateJob(imapItem, imapCollection);
  if (!cjob->exec()) {
    kWarning() << "Can't create imap item " << imapItem.id() << cjob->errorString();
  }

  imapItem = cjob->item();
  handler->itemAdded(imapItem);
  m_excludeAppend << imapItem.id();

  kolabItem.setRemoteId(QString::number(imapItem.id()));
  changeCommitted( kolabItem );
}

void KolabProxyResource::itemChanged( const Item &kolabItem, const QSet<QByteArray> &parts )
{
  kDebug() << "ITEMCHANGED" << kolabItem.id() << kolabItem.remoteId();

  Item imapItem;
  ItemFetchJob* job = new ItemFetchJob( Item(kolabItem.remoteId().toUInt()) );
  job->fetchScope().fetchFullPayload();
  if (job->exec()) {
    Item::List items = job->items();
    imapItem = items[0];
  } else {
    kWarning() << "Can't fetch imap item " << kolabItem.remoteId();
  }

  KolabHandler *handler = m_monitoredCollections.value(imapItem.storageCollectionId());
  if (!handler) {
    kWarning() << "No handler found";
    cancelTask();
    return;
  }

  handler->toKolabFormat(kolabItem, imapItem);
  ItemModifyJob *mjob = new ItemModifyJob( imapItem );
    if (!mjob->exec()) {
      kWarning() << "Can't modify imap item " << imapItem.id();
    }

  changeCommitted( kolabItem );
}

void KolabProxyResource::itemRemoved( const Item &item )
{
  kDebug() << "ITEMREMOVED";
  kDebug() << "Item removed " << item.id() << item.remoteId();
  const Item imapItem( item.remoteId().toUInt() );
  ItemDeleteJob *djob = new ItemDeleteJob( imapItem );
  changeCommitted( item );
  Q_UNUSED(djob);
}

void KolabProxyResource::collectionAdded(const Akonadi::Collection& collection, const Akonadi::Collection& parent)
{
  Q_ASSERT( !KolabHandler::kolabTypeForCollection( collection ).isEmpty() );

  Collection imapCollection( collection );
  imapCollection.setId( -1 );
  imapCollection.setRemoteId( QString() );
  Collection imapParent( parent.remoteId().toLongLong() );
  imapCollection.setParent( imapParent );
  CollectionAnnotationsAttribute* attr =
    imapCollection.attribute<CollectionAnnotationsAttribute>( Collection::AddIfMissing );
  QMap<QByteArray, QByteArray> annotations = attr->annotations();
  annotations["/vendor/kolab/folder-type"] = KolabHandler::kolabTypeForCollection( collection );
  attr->setAnnotations( annotations );

  CollectionCreateJob *job = new CollectionCreateJob( imapCollection, this );
  // TODO wait for the result
  changeCommitted( collection );
}

void KolabProxyResource::collectionChanged(const Akonadi::Collection& collection)
{
  Collection imapCollection;
  imapCollection.setId( collection.remoteId().toLongLong() );
  imapCollection.setName( collection.name() );
  imapCollection.setCachePolicy( collection.cachePolicy() );
  imapCollection.setRights( collection.rights() );

  CollectionModifyJob *job = new CollectionModifyJob( imapCollection, this );
  // TODO wait for the result
  changeCommitted( collection );
}

void KolabProxyResource::collectionRemoved(const Akonadi::Collection& collection)
{
  Collection imapCollection;
  imapCollection.setId( collection.remoteId().toLongLong() );

  CollectionDeleteJob *job = new CollectionDeleteJob( imapCollection, this );
  // TODO wait for result
  changeCommitted( collection );
}

void KolabProxyResource::deleteImapItem(const Item& item)
{
  kDebug() << "DELETEIMAPITEM";
  ItemDeleteJob *djob = new ItemDeleteJob( item );
  Q_UNUSED(djob);
}

void KolabProxyResource::addImapItem(const Item& item, Akonadi::Entity::Id collectionId)
{
  kDebug() << "ADDITEMTOIMAP";
  new ItemCreateJob( item, Collection(collectionId) );
}

void KolabProxyResource::imapItemAdded(const Item& item, const Collection &collection)
{
  kDebug() << "imapItemAdded " << item.id() << collection.id() << Collection::root().id();
  if (m_excludeAppend.contains(item.id()))   {
    kDebug() << "item already present";
    m_excludeAppend.removeAll(item.id());
    return;
  }
//TODO: slow, would be nice if ItemCreateJob would work with a Collection having only the remoteId set
  CollectionFetchJob *job = new CollectionFetchJob( Collection::root(), CollectionFetchJob::Recursive );
  connect(job, SIGNAL(result(KJob*)), this, SLOT(collectionFetchDone(KJob *)));
  m_ids[job] = QString::number(collection.id());
  m_items[job] = item;
}

void KolabProxyResource::collectionFetchDone(KJob *job)
{
  if ( job->error() ) {
    kWarning( ) << "Error on collection fetch:" << job->errorText();
  } else {
    Collection c;
    Collection::List collections = qobject_cast<CollectionFetchJob*>(job)->collections();
    foreach( const Collection &col, collections ) {
      if (col.remoteId() == m_ids[job]) {
        c = col;
        break;
      }
    }

    KolabHandler *handler = m_monitoredCollections.value(c.remoteId().toUInt());
    if (!handler) {
      kWarning() << "No handler found";
      return;
    }

    Item::List newItems = handler->translateItems(Item::List() << m_items[job]);
    if (!newItems.isEmpty()) {
      ItemCreateJob *cjob = new ItemCreateJob( newItems[0],  c );
      connect(cjob, SIGNAL(result(KJob*)), this, SLOT(itemCreatedDone(KJob *)));
    }
  }
  m_ids.remove(job);
  m_items.remove(job);
}

void KolabProxyResource::itemCreatedDone(KJob *job)
{
  if ( job->error() ) {
    kWarning( ) << "Error on creating item:" << job->errorText();
  } else {
  }
}

void KolabProxyResource::imapItemRemoved(const Item& item)
{
  kDebug() << "IMAPITEMREMOVED";
  Item kolabItem;
  kolabItem.setRemoteId( QString::number( item.id() ) );
  Q_FOREACH(KolabHandler *handler, m_monitoredCollections.values()) {
    handler->itemDeleted(item);
  }
  ItemDeleteJob *job = new ItemDeleteJob( kolabItem );
}

void KolabProxyResource::imapCollectionAdded(const Collection &collection, const Collection &parent)
{
  if ( collection.resource() == identifier() ) // just to be sure...
    return;

  kDebug() << "IMAPCOLLECTIONADDED";
  if ( m_monitoredCollections.contains(collection.id()) ) {
    // something is wrong, so better reload out collection tree
    kDebug() << "IMAPCOLLECTIONADDED ABORT";
    synchronizeCollectionTree();
    return;
  }

  if ( registerHandlerForCollection( collection ) ) {
    const Collection kolabCollection = createCollection( collection );
    CollectionCreateJob *job = new CollectionCreateJob( kolabCollection, this );
    connect( job, SIGNAL(result(KJob*)), SLOT(imapFolderChangeResult(KJob*)) );
  }
}

void KolabProxyResource::imapCollectionChanged(const Collection &collection)
{
  if ( collection.resource() == identifier() ) // just to be sure...
    return;

  kDebug() << "IMAPCOLLECTIONCHANGED";
  if ( !m_monitoredCollections.contains(collection.id()) ) {
    // something is wrong, so better reload out collection tree
    kDebug() << "IMAPCOLLECTIONCHANGED ABORTED";
    synchronizeCollectionTree();
    return;
  }

  Collection kolabCollection = createCollection( collection );
  CollectionModifyJob *job = new CollectionModifyJob( kolabCollection, this );
  connect( job, SIGNAL(result(KJob*)), SLOT(imapFolderChangeResult(KJob*)) );
}

void KolabProxyResource::imapFolderChangeResult(KJob* job)
{
  if ( job->error() ) {
    // something went wrong or the change was too complex to handle in the above slots,
    // so re-sync the entire tree.
    kDebug() << "Re-syncing collection tree as incremental changes did not succeed." << job->errorText();
    synchronizeCollectionTree();
  }
}

void KolabProxyResource::imapCollectionRemoved(const Collection &collection)
{
  if ( collection.resource() == identifier() ) // just to be sure...
    return;

  kDebug() << "IMAPCOLLECTIONREMOVED";
  Collection c;
  CollectionFetchJob *job = new CollectionFetchJob( Collection::root(), CollectionFetchJob::Recursive );
  if ( job->exec() ) {
    Collection::List collections = job->collections();

    foreach( const Collection &col, collections ) {
      if (col.remoteId() == QString::number(collection.id())) {
        c = col;
        break;
      }
    }
  } else {
    kWarning() << "Can't find the matching collection for  " << collection.id();
    return;
  }

  KolabHandler *handler = m_monitoredCollections.value(collection.id());
  delete handler;
  m_monitoredCollections.remove(collection.id());
  new CollectionDeleteJob(c);
}

Collection KolabProxyResource::createCollection(const Collection& imapCollection)
{
  Collection c;
  if ( imapCollection.parentRemoteId() == m_root.remoteId() )
    c.setParent( m_root );
  else
    c.setParentRemoteId( QString::number( imapCollection.parent() ) );
  QString origName = imapCollection.name();
  QString name = origName;
  c.setName( name );
  KolabHandler *handler = m_monitoredCollections.value(imapCollection.id());
  QStringList contentTypes;
  contentTypes.append( Collection::mimeType() );
  if ( handler )
    contentTypes.append( handler->contentMimeTypes() );
  c.setContentMimeTypes( contentTypes );
  c.setRights( imapCollection.rights() );
  c.setRemoteId(QString::number(imapCollection.id()));

  return c;
}

bool KolabProxyResource::registerHandlerForCollection(const Akonadi::Collection& imapCollection)
{
  CollectionAnnotationsAttribute *annotationsAttribute =
      imapCollection.attribute<CollectionAnnotationsAttribute>();
  if ( annotationsAttribute ) {
    QMap<QByteArray, QByteArray> annotations = annotationsAttribute->annotations();

    KolabHandler *handler = KolabHandler::createHandler(annotations["/vendor/kolab/folder-type"]);
    if ( handler ) {
      connect(handler, SIGNAL(deleteItemFromImap(const Akonadi::Item&)), this, SLOT(deleteImapItem(const Akonadi::Item&)));
      connect(handler, SIGNAL(addItemToImap(const Akonadi::Item&, Akonadi::Entity::Id)), this, SLOT(addImapItem(const Akonadi::Item&, Akonadi::Entity::Id)));
      m_monitor->setCollectionMonitored(imapCollection);
      m_monitoredCollections.insert(imapCollection.id(), handler);
      return true;
    }
  }

  return false;
}

AKONADI_RESOURCE_MAIN( KolabProxyResource )

#include "kolabproxyresource.moc"
