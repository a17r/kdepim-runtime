/*
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

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

#ifndef AKONADI_XMLCOMPARATOR_H
#define AKONADI_XMLCOMPARATOR_H

#include <akonadi/collection.h>
#include <akonadi/item.h>
#include <akonadi/xml/xmldocument.h>

#include <QtCore/QMetaEnum>
#include <QtCore/QObject>
#include <QtCore/QTextStream>

#include <boost/bind.hpp>
#include <algorithm>


/**
  Compares a Akonadi collection sub-tree with reference data supplied in an XML file.
*/
class XmlOperations : public QObject
{
  Q_OBJECT
  Q_ENUMS( CollectionField )

  public:
    XmlOperations( QObject *parent = 0 );
    ~XmlOperations();

    enum CollectionField {
      None = 0,
      RemoteId = 1,
      Name = 2,
      ContentMimeType = 4
    };

    Q_DECLARE_FLAGS( CollectionFields, CollectionField )

    void setCollectionKey( CollectionField field );
    void ignoreCollectionField( CollectionField field );

  public slots:
    void setRootCollections( const QString &resourceId );
    void setRootCollections( const Akonadi::Collection::List &roots );
    void setXmlFile( const QString &fileName );

    Akonadi::Item getItemByRemoteId(const QString& rid);
    Akonadi::Collection getCollectionByRemoteId(const QString& rid);

    void setCollectionKey( const QString &fieldName );
    void ignoreCollectionField( const QString &fieldName );

    bool compare();
    void assertEqual();

    QString lastError() const;

    bool compareCollections( const Akonadi::Collection::List &cols, const Akonadi::Collection::List &refCols );
    bool compareCollection( const Akonadi::Collection &col, const Akonadi::Collection &refCol );
    bool compareItems( const Akonadi::Item::List &items, const Akonadi::Item::List &refItems );
    bool compareItem( const Akonadi::Item &item, const Akonadi::Item &refItem );
    bool compareAttributes( const Akonadi::Entity &entity, const Akonadi::Entity &refEntity );
    bool hasItem(const Akonadi::Item& _item, const Akonadi::Collection& _col);
    bool hasItem(const Akonadi::Item& _item, const QString& rid);

  private:
    template <typename T> bool compareValue( const Akonadi::Collection &col, const Akonadi::Collection &refCol,
                                             T (Akonadi::Collection::*property)() const,
                                             CollectionField propertyType );
    template <typename T> bool compareValue( const Akonadi::Collection &col, const Akonadi::Collection &refCol,
                                             T (Akonadi::Entity::*property)() const,
                                             CollectionField propertyType );
    template <typename T> bool compareValue( const Akonadi::Item& item, const Akonadi::Item& refItem,
                                             T (Akonadi::Item::*property)() const,
                                             const char* propertyName );
    template <typename T> bool compareValue( const T& value, const T& refValue );

    template <typename T> void sortCollectionList( Akonadi::Collection::List &list,
                                                   T ( Akonadi::Collection::*property)() const ) const;
    template <typename T> void sortCollectionList( Akonadi::Collection::List &list,
                                                   T ( Akonadi::Entity::*property)() const ) const;

  private:
    Akonadi::Collection::List mRoots;
    Akonadi::XmlDocument mDocument;
    QString mErrorMsg;
    CollectionFields mCollectionFields;
    CollectionField mCollectionKey;
};


template <typename T>
bool XmlOperations::compareValue( const Akonadi::Collection& col, const Akonadi::Collection& refCol,
                                  T (Akonadi::Collection::*property)() const,
                                  CollectionField propertyType )
{
  if ( mCollectionFields & propertyType ) {
    const bool result = compareValue<T>( ((col).*(property))(), ((refCol).*(property))() );
    if ( !result ) {
      const QMetaEnum me = metaObject()->enumerator( metaObject()->indexOfEnumerator( "CollectionField" ) );
      mErrorMsg.prepend( QString::fromLatin1( "Collection with remote id '%1' differs in property '%2':\n" )
      .arg( col.remoteId() ).arg( me.valueToKey( propertyType ) ) );
    }
    return result;
  }
  return true;
}

template <typename T>
bool XmlOperations::compareValue( const Akonadi::Collection& col, const Akonadi::Collection& refCol,
                                  T (Akonadi::Entity::*property)() const,
                                  CollectionField propertyType )
{
  if ( mCollectionFields & propertyType ) {
    const bool result = compareValue<T>( ((col).*(property))(), ((refCol).*(property))() );
    if ( !result ) {
      const QMetaEnum me = metaObject()->enumerator( metaObject()->indexOfEnumerator( "CollectionField" ) );
      mErrorMsg.prepend( QString::fromLatin1( "Collection with remote id '%1' differs in property '%2':\n" )
      .arg( col.remoteId() ).arg( me.valueToKey( propertyType ) ) );
    }
    return result;
  }
  return true;
}

template <typename T>
bool XmlOperations::compareValue( const Akonadi::Item& item, const Akonadi::Item& refItem,
                                  T (Akonadi::Item::*property)() const,
                                  const char* propertyName )
{
  const bool result = compareValue<T>( ((item).*(property))(), ((refItem).*(property))() );
  if ( !result ) {
    mErrorMsg.prepend( QString::fromLatin1( "Item with remote id '%1' differs in property '%2':\n" )
      .arg( item.remoteId() ).arg( propertyName ) );
  }
  return result;
}

template <typename T>
bool XmlOperations::compareValue(const T& value, const T& refValue )
{
  if ( value == refValue )
    return true;
  QTextStream ts( &mErrorMsg );
  ts << " Actual: " << value << endl << " Expected: " << refValue;
  return false;
}

template <typename T>
void XmlOperations::sortCollectionList( Akonadi::Collection::List &list,
                                        T ( Akonadi::Collection::*property)() const ) const
{
  std::sort( list.begin(), list.end(), boost::bind( property, _1 ) < boost::bind( property, _2 ) );
}

template <typename T>
void XmlOperations::sortCollectionList( Akonadi::Collection::List &list,
                                        T ( Akonadi::Entity::*property)() const ) const
{
  std::sort( list.begin(), list.end(), boost::bind( property, _1 ) < boost::bind( property, _2 ) );
}

#endif
