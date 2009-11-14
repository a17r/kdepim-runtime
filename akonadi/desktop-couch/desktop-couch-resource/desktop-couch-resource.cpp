/*
    Copyright (c) 2009 Canonical

    Author: Till Adam <till@kdab.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2.1 or version 3 of the license,
    or later versions approved by the membership of KDE e.V.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#include "desktop-couch-resource.h"
//#include "settingsadaptor.h"

#include <akonadi/changerecorder.h>
#include <akonadi/itemfetchscope.h>
#include <akonadi/entitydisplayattribute.h>
#include <akonadi/collection.h>

#include <kfiledialog.h>
#include <klocale.h>
#include <KWindowSystem>

#include <QtDBus/QDBusConnection>
#include <QDebug>
using namespace Akonadi;
using KABC::PhoneNumber;

DesktopCouchResource::DesktopCouchResource( const QString &id )
  : ResourceBase( id )
{
  setName( i18n("Desktop Couch Address Book") );
//  setSupportedMimetypes( QStringList() << KABC::Addressee::mimeType(), "office-address-book" );
/*
  new SettingsAdaptor( Settings::self() );
  QDBusConnection::sessionBus().registerObject( QLatin1String( "/Settings" ),
                            Settings::self(), QDBusConnection::ExportAdaptors );
                            */
  m_root.setParent( Collection::root() );
  m_root.setRemoteId( identifier() );
  m_root.setName( identifier() );

  QStringList mimeTypes;
  mimeTypes << "text/directory";
  m_root.setContentMimeTypes( mimeTypes );

  EntityDisplayAttribute *attr = m_root.attribute<EntityDisplayAttribute>( Collection::AddIfMissing );
  attr->setDisplayName( i18n("Desktop Couch Address Book") );
  attr->setIconName( "couchdb" );

  changeRecorder()->itemFetchScope().fetchFullPayload();
  synchronizeCollectionTree();
}

DesktopCouchResource::~DesktopCouchResource()
{
}

bool DesktopCouchResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
  Q_UNUSED( parts );
  const QString rid = item.remoteId();
  m_db.disconnect( this, SLOT( slotDocumentRetrieved(QVariant) ) );
  m_db.connect( &m_db, SIGNAL( documentRetrieved(QVariant) ),
              this, SLOT( slotDocumentRetrieved(QVariant) ) );
  CouchDBDocumentInfo info;
  info.setId( rid );
  info.setDatabase( "contacts" );
  m_db.requestDocument( info );

  setProperty( "akonadiItem", QVariant::fromValue(item) );

  return true;
}

static QVariant addresseeToVariant( const KABC::Addressee& a )
{
    QVariantMap vMap;
    vMap.insert( "_id", a.uid() );
    vMap.insert( "_rev", a.custom( "akonadi-desktop-couch-resource", "_rev" ) );

    // FIXME handle known fields
    // FIXME restore unknown fields from customs
    return vMap;
}

static KABC::Addressee variantToAddressee( const QVariant& v )
{
  KABC::Addressee a;
  QVariantMap vMap = v.toMap();
  a.setUid( vMap["_id"].toString() );
  a.setGivenName( vMap["first_name"].toString() );
  a.setFamilyName( vMap["last_name"].toString() );

  // extract email addresses
  const QVariantMap emails = vMap["email_addresses"].toMap();
  Q_FOREACH( QVariant email, emails ) {
    QVariantMap emailmap = email.toMap();
    const QString emailstr = emailmap["address"].toString();
    if ( !emailstr.isEmpty() )
      a.insertEmail( emailstr );
  }

  // birthday
  a.setBirthday( QDateTime::fromString( vMap["birth_date"].toString() ) );

  //phone numbers
  const QVariantMap numbers = vMap["phone_numbers"].toMap();
  Q_FOREACH( QVariant number, numbers ) {
    QVariantMap numbermap = number.toMap();
    const QString numberstr = numbermap["number"].toString();
    if ( !numberstr.isEmpty() ) {
      PhoneNumber phonenumber;
      phonenumber.setNumber(numberstr);
      phonenumber.setId(numbermap["description"].toString());

      // FIXME type

      // FIXME priority
      a.insertPhoneNumber( phonenumber );
    }
  }
  a.insertCustom( "akonadi-desktop-couch-resource", "_rev", vMap["_rev"].toString() );

  // FIXME enter all other fields as custom headers as well

  return a;
}

void DesktopCouchResource::slotDocumentRetrieved( const QVariant& v )
{


  KABC::Addressee a( variantToAddressee( v ) );

  Item i( property("akonadiItem").value<Item>() );
  Q_ASSERT( i.remoteId() == a.uid() );
  i.setMimeType( KABC::Addressee::mimeType() );
  i.setPayload<KABC::Addressee>( a );
  itemRetrieved( i );
}

void DesktopCouchResource::aboutToQuit()
{
  //Settings::self()->writeConfig();
}

void DesktopCouchResource::configure( WId windowId )
{
    /*
  SingleFileResourceConfigDialog<Settings> dlg( windowId );
  dlg.setFilter( "*.vcf|" + i18nc("Filedialog filter for *.vcf", "vCard Address Book File" ) );
  dlg.setCaption( i18n("Select Address Book") );
  if ( dlg.exec() == QDialog::Accepted ) {
    reloadFile();
  }
  */
}

void DesktopCouchResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection& )
{
  KABC::Addressee addressee;
  if ( item.hasPayload<KABC::Addressee>() )
    addressee  = item.payload<KABC::Addressee>();

  if ( !addressee.isEmpty() ) {
//    mAddressees.insert( addressee.uid(), addressee );

    Item i( item );
    i.setRemoteId( addressee.uid() );
    changeCommitted( i );

  } else {
    changeProcessed();
  }
}

void DesktopCouchResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray>& )
{
  KABC::Addressee addressee;
  if ( item.hasPayload<KABC::Addressee>() )
    addressee  = item.payload<KABC::Addressee>();

  if ( !addressee.isEmpty() ) {
    
    CouchDBDocumentInfo info;
    info.setId( item.remoteId() );
    info.setDatabase( "contacts" );
    QVariant v = addresseeToVariant( addressee );
    m_db.updateDocument( info, v );

    // FIXME make async and check error 
    // setProperty( "akonadiItem", QVariant::fromValue(item) );
    Item i( item );
    i.setRemoteId( addressee.uid() );
    changeCommitted( i );

  } else {
    changeProcessed();
  }
}

void DesktopCouchResource::itemRemoved(const Akonadi::Item & item)
{
//  if ( mAddressees.contains( item.remoteId() ) )
//    mAddressees.remove( item.remoteId() );


  changeProcessed();
}

void DesktopCouchResource::retrieveItems( const Akonadi::Collection & col )
{
  // CouchDB does not support folders so we can safely ignore the collection
  Q_UNUSED( col );

  m_db.disconnect( this, SLOT( slotDocumentsListed(CouchDBDocumentInfoList) ) );
  m_db.connect( &m_db, SIGNAL( documentsListed( CouchDBDocumentInfoList ) ),
              this, SLOT( slotDocumentsListed(CouchDBDocumentInfoList) ) );
  m_db.requestDocumentListing( "contacts" );
}

void DesktopCouchResource::slotDocumentsListed( const CouchDBDocumentInfoList& list )
{
  Item::List items;
  Q_FOREACH( const CouchDBDocumentInfo& info, list ) {
    Item item;
    item.setRemoteId( info.id() );
    item.setMimeType( KABC::Addressee::mimeType() );
    items.append( item );
  }

  itemsRetrieved( items );
}


void DesktopCouchResource::retrieveCollections()
{
  Collection::List list;
  list << m_root;
  collectionsRetrieved( list );
}

AKONADI_RESOURCE_MAIN( DesktopCouchResource )

#include "desktop-couch-resource.moc"
