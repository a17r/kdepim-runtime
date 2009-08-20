/*
    This file is part of KJots.

    Copyright (c) 2008 Stephen Kelly <steveire@gmail.com>

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

#include "kjotsresource.h"

#include <QtCore/QDir>
#include <QtDBus/QDBusConnection>
#include <QtXml/QDomDocument>

#include <akonadi/changerecorder.h>
#include <akonadi/attributefactory.h>
#include <akonadi/entitydisplayattribute.h>
#include "collectionchildorderattribute.h"
#include <akonadi/itemfetchscope.h>
#include <akonadi/collectionfetchjob.h>
#include <akonadi/itemfetchjob.h>

#include <KUrl>
#include <KStandardDirs>
#include <kfiledialog.h>
#include <klocalizedstring.h>
#include <KTemporaryFile>

#include <kdebug.h>

#include "kjotspage.h"

#include "datadir.h"

using namespace Akonadi;

QString KJotsResource::findParent( const QString &remoteId )
{
  QHashIterator<QString, QStringList> i( m_parentBook );
  while ( i.hasNext() ) {
    i.next();
    if ( i.value().contains( remoteId ) ) {
      return i.key();
    }
  }
  return QString();
}

QString KJotsResource::getFileUrl( const Collection &col ) const
{
  return m_rootDataPath + '/' + col.remoteId();
}

QString KJotsResource::getFileUrl( const Item &item ) const
{
  return m_rootDataPath + '/' + item.remoteId();
}

Collection::List KJotsResource::getDescendantCollections(Collection& col) const
{
  Collection::List list;
  QString fileUrl = getFileUrl( col );
  KUrl bookUrl( fileUrl );
  QFile bookFile( bookUrl.toLocalFile() );

  QDomDocument doc;
  doc.setContent( &bookFile );

  QDomElement rootElement = doc.documentElement();

  QDomElement title = rootElement.firstChildElement( "Title" );
  QString bookname = title.text();

  if (col.parentCollection() != Collection::root() )
  {
    EntityDisplayAttribute *displayAttribute = new EntityDisplayAttribute();
    displayAttribute->setDisplayName( bookname );
    displayAttribute->setIconName( "x-office-address-book" );
    col.addAttribute( displayAttribute );
  }
  col.setName( bookname );

  QDomElement contents = rootElement.firstChildElement( "Contents" );

  QDomElement entity = contents.firstChildElement();
  QString filename;
  while ( !entity.isNull() ) {
    if ( entity.tagName() == "KJotsBook" ) {
      filename = entity.attribute( "filename" );
      Collection child;
      child.setRemoteId( filename );
      child.setContentMimeTypes( QStringList() << Collection::mimeType() << KJotsPage::mimeType() );
      child.setParentCollection( col );
      list << getDescendantCollections( child );
      list << child;
    }
    entity = entity.nextSiblingElement();
  }

  return list;
}

Item::List KJotsResource::getContainedItems( const Collection &col ) const
{
  Item::List list;

  QString fileUrl = getFileUrl( col );
  KUrl bookUrl( fileUrl );
  QFile bookFile( bookUrl.toLocalFile() );

  QDomDocument doc;
  doc.setContent( &bookFile );

  QDomElement rootElement = doc.documentElement();

  QDomElement contents = rootElement.firstChildElement( "Contents" );

  QDomElement entity = contents.firstChildElement();
  QString filename;
  while ( !entity.isNull() ) {
    filename = entity.attribute( "filename" ) ;
    if ( entity.tagName() == "KJotsPage" ) {
      Item item;
      item.setRemoteId( filename );
      item.setMimeType( KJotsPage::mimeType() );
      item.setParentCollection( col );
      list << item;
    }
    entity = entity.nextSiblingElement();
  }

  return list;
}


KJotsResource::KJotsResource( const QString &id )
    : ResourceBase( id )
{
  kDebug() << DATADIR;
  changeRecorder()->fetchCollection( true );
  changeRecorder()->itemFetchScope().fetchFullPayload();
  changeRecorder()->itemFetchScope().fetchAttribute<CollectionChildOrderAttribute>();
  changeRecorder()->itemFetchScope().fetchAttribute<EntityDisplayAttribute>();
  setHierarchicalRemoteIdentifiersEnabled(true);

  // Temporary for development.
  m_rootDataPath = QString( DATADIR );
  //     m_rootDataPath = KStandardDirs::locateLocal( "data", "kjots/" );

  AttributeFactory::registerAttribute<CollectionChildOrderAttribute>();
}


KJotsResource::~KJotsResource()
{
}

void KJotsResource::retrieveCollections()
{
  Collection::List list;

  Collection resourceRootCollection;
  resourceRootCollection.setRemoteId( "/kjots.bookshelf" );
  resourceRootCollection.setParentCollection( Collection::root() );
  // The bookshelf can not contain pages.
  resourceRootCollection.setContentMimeTypes( QStringList()
                                              << Collection::mimeType() );

  list << getDescendantCollections(resourceRootCollection);
  list << resourceRootCollection;
  collectionsRetrieved( list );

}

void KJotsResource::retrieveItems( const Akonadi::Collection &collection )
{
  Item::List items = getContainedItems( collection );
  itemsRetrieved( items );
}

KJotsPage KJotsResource::getPage( const Akonadi::Item& item, QSet<QByteArray> parts ) const
{
  QString fileUrl = getFileUrl( item );
  KUrl pageUrl( fileUrl );
  QFile pageFile( pageUrl.toLocalFile() );

  KJotsPage page;
  QDomDocument doc;
  doc.setContent( &pageFile );

  QDomElement pageRootElement = doc.documentElement();

  if ( pageRootElement.tagName() ==  "KJotsPage" ) {
    QDomNode n = pageRootElement.firstChild();
    while ( !n.isNull() ) {
      QDomElement e = n.toElement();
      if ( !e.isNull() ) {
        if ( e.tagName() == "Title" ) {
          if ( parts.contains( "title" ) || parts.contains( Item::FullPayload ) )
            page.setTitle( e.text() );
        }

        if ( e.tagName() == "Content" ) {
          if ( parts.contains( "content" ) || parts.contains( Item::FullPayload ) )
            page.setContent( QString( e.text() ) );
        }
      }
      n = n.nextSibling();
    }
  }

  return page;
}

bool KJotsResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
  Item newItem = item;

  KJotsPage page = getPage( item, parts );

  newItem.setPayload<KJotsPage>( page );

  EntityDisplayAttribute *displayAttribute = new EntityDisplayAttribute();
  displayAttribute->setDisplayName( page.title() );
  displayAttribute->setIconName( "text-x-generic" );
  newItem.addAttribute( displayAttribute );

  itemRetrieved( newItem );
  return true;
}

void KJotsResource::aboutToQuit()
{
  // TODO: any cleanup you need to do while there is still an active
  // event loop. The resource will terminate after this method returns
}

void KJotsResource::configure( WId windowId )
{
  Q_UNUSED( windowId );

  // This resource doesn't need to be configured.

  synchronize();
}

void KJotsResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{

  changeCommitted( item );
}

void KJotsResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
  changeCommitted( item );
}

void KJotsResource::itemRemoved( const Akonadi::Item &item )
{
  changeCommitted( item );

}

void KJotsResource::itemMoved( const Akonadi::Item &item,
                               const Akonadi::Collection &source,
                               const Akonadi::Collection &destination )
{

}

void KJotsResource::collectionAdded( const Akonadi::Collection &collection, const Akonadi::Collection &parent )
{
  changeCommitted( collection );
}

void KJotsResource::collectionRemoved( const Akonadi::Collection &collection )
{
  changeCommitted( collection );
}

void KJotsResource::collectionChanged( const Akonadi::Collection &collection )
{
  changeCommitted( collection );

}

void KJotsResource::collectionMoved( const Akonadi::Collection &collection,
                                     const Akonadi::Collection &source,
                                     const Akonadi::Collection &destination )
{

}

AKONADI_RESOURCE_MAIN( KJotsResource )

#include "kjotsresource.moc"



