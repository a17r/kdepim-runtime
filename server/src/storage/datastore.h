/***************************************************************************
 *   Copyright (C) 2006 by Andreas Gungl <a.gungl@gmx.de>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#ifndef DATASTORE_H
#define DATASTORE_H

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>
#include <QtSql/QSqlDatabase>

class QSqlQuery;

#include "akonadi_export.h"
#include "entities.h"
#include "fetchquery.h"
#include "notificationcollector.h"

class OrgKdeAkonadiResourceInterface;

namespace Akonadi {

class FetchQuery;
class NotificationCollector;

/**
  This class handles all the database access.

  <h4>Database configuration</h4>

  You can select between various database backends during runtime using the
  @c $HOME/.config/akonadi/akonadiserverrc configuration file.

  Example:
@verbatim
[%General]
Driver=QMYSQL

[QMYSQL_EMBEDDED]
Name=akonadi
Options=SERVER_DATADIR=/home/foo/.local/share/akonadi/db_data

[QMYSQL]
Name=akonadi
Host=localhost
User=foo
Password=*****
#Options=UNIX_SOCKET=/home/foo/.local/share/akonadi/db_misc/mysql.socket

[QSQLITE]
Name=/home/foo/.local/share/akonadi/akonadi.db
@endverbatim

  Use @c General/Driver to select the QSql driver to use for databse
  access. The following drivers are currently supported, other might work
  but are untested:

  - QMYSQL
  - QMYSQL_EMBEDDED
  - QSQLITE

  The options for each driver are read from the corresponding group.
  The following options are supported, dependent on the driver not all of them
  might have an effect:

  - Name: Database name, for sqlite that's the file name of the database.
  - Host: Hostname of the database server
  - User: Username for the database server
  - Password: Password for the database server
  - Options: Additional options, format is driver-dependent
*/
class AKONADI_SERVER_EXPORT DataStore : public QObject
{
    Q_OBJECT
  public:
    /**
      Closes the database connection and destroys the DataStore object.
    */
    virtual ~DataStore();

    /**
      Opens the database connection.
    */
    void open();

    /**
      Closes the databse connection.
    */
    void close();

    /**
      Initializes the database. Should be called during startup by the main thread.
    */
    bool init();

    /**
      Per thread signleton.
    */
    static DataStore* self();

    /* --- Flag ---------------------------------------------------------- */
    bool appendFlag( const QString & name );

    /* --- ItemFlags ----------------------------------------------------- */
    bool setItemFlags( const PimItem &item, const QList<Flag> &flags );
    bool appendItemFlags( const PimItem &item, const QList<Flag> &flags );
    bool appendItemFlags( const PimItem &item, const QList<QByteArray> &flags );
    bool removeItemFlags( const PimItem &item, const QList<Flag> &flags );

    /* --- Location ------------------------------------------------------ */
    bool appendLocation( Location &location );
    /// removes the given location and all its content
    bool cleanupLocation( Location &location );
    bool updateLocationCounts( const Location & location, int existsChange, int recentChange, int unseenChange );
    bool changeLocationPolicy( Location & location, const CachePolicy & policy );
    bool resetLocationPolicy( const Location & location );
    /// rename the collection @p location to @p newName.
    bool renameLocation( const Location &location, int newParent, const QString &newName );

    bool appendMimeTypeForLocation( int locationId, const QString & mimeType );
    bool appendMimeTypeForLocation( int locationId, int mimeTypeId );
    bool removeMimeTypesForLocation( int locationId );

    static QString locationDelimiter() { return QLatin1String("/"); }

    /**
      Returns the active CachePolicy for this Location.
    */
    CachePolicy activeCachePolicy( const Location &loc );

    /* --- MimeType ------------------------------------------------------ */
    bool appendMimeType( const QString & mimetype, int *insertId = 0 );

    /* --- PimItem ------------------------------------------------------- */
    bool appendPimItem( const QByteArray & data,
                        const MimeType & mimetype,
                        const Location & location,
                        const QDateTime & dateTime,
                        const QByteArray & remote_id,
                        PimItem &pimItem );
    bool updatePimItem( PimItem &pimItem, const QByteArray &data );
    bool updatePimItem( PimItem &pimItem, const Location &destination );
    bool updatePimItem( PimItem &pimItem, const QString &remoteId );
    PimItem pimItemById( int id, FetchQuery::Type type = FetchQuery::FastType );

    QList<PimItem> listPimItems( const Location & location, const Flag &flag );

    /**
     * Removes the pim item and all referenced data ( e.g. flags )
     */
    bool cleanupPimItem( const PimItem &item );

    /**
     * Cleanups all items which have the '\\Deleted' flag set
     */
    bool cleanupPimItems( const Location &location );

    int highestPimItemId() const;
    int highestPimItemCountByLocation( const Location &location );

    QList<PimItem> matchingPimItemsByUID( const QList<QByteArray> &sequences,
                                          const Location & l = Location(),
                                          FetchQuery::Type type = FetchQuery::FastType );
    QList<PimItem> matchingPimItemsBySequenceNumbers( const QList<QByteArray> &sequences,
                                                      const Location &location,
                                                      FetchQuery::Type type = FetchQuery::FastType );

    QList<PimItem> fetchMatchingPimItemsByUID( const FetchQuery &query, const Location& l = Location() );
    QList<PimItem> fetchMatchingPimItemsBySequenceNumbers( const FetchQuery &query,
                                                           const Location &location );

    /* --- Collection attribues ------------------------------------------ */
    bool addCollectionAttribute( const Location &loc, const QByteArray &key, const QByteArray &value );
    bool removeCollectionAttribute( const Location &loc, const QByteArray &key );

    /* --- Helper functions ---------------------------------------------- */
    /** Returns the id of the next PIM item that is added to the db.
        @return possible id of the next PIM item that is added to the database
     */
    int uidNext() const;

    QString storagePath() const;

    /**
      Begins a transaction. No changes will be written to the database and
      no notification signal will be emitted unless you call commitTransaction().
      @return @c true if successful.
    */
    bool beginTransaction();

    /**
      Reverts all changes within the current transaction.
    */
    bool rollbackTransaction();

    /**
      Commits all changes within the current transaction and emits all
      collected notfication signals. If committing fails, the transaction
      will be rolled back.
    */
    bool commitTransaction();

    /**
      Returns true if there is a transaction in progress.
    */
    bool inTransaction() const;

    /**
      Returns the notification collector of this DataStore object.
      Use this to listen to change notification signals.
    */
    NotificationCollector* notificationCollector() const { return mNotificationCollector; }

    /**
      Returns the QSqlDatabase object. Use this for generating queries yourself.
    */
    QSqlDatabase database() const { return m_database; }

    /**
      Sets the current session id.
    */
    void setSessionId( const QByteArray &sessionId ) { mSessionId = sessionId; }

    /**
      Returns the name of the used database driver.
    */
    static QString driverName() { return mDbDriverName; }

Q_SIGNALS:
    /**
      Emitted if a transaction has been successfully committed.
    */
    void transactionCommitted();
    /**
      Emitted if a transaction has been aborted.
    */
    void transactionRolledBack();

protected:
    /**
      Creates a new DataStore object and opens it.
    */
    DataStore();

    void debugLastDbError( const char* actionDescription ) const;
    void debugLastQueryError( const QSqlQuery &query, const char* actionDescription ) const;
    QByteArray retrieveDataFromResource( int uid, const QByteArray& remote_id,
                                         int locationid, FetchQuery::Type type );

  public:
    /** Returns the id of the most recent inserted row, or -1 if there's no such
        id.
        @param query the query we want to get the last insert id for
        @return id of the most recent inserted row, or -1
     */
    static int lastInsertId( const QSqlQuery & query );

  private:
    /** Converts the given date/time to the database format, i.e.
        "YYYY-MM-DD HH:MM:SS".
        @param dateTime the date/time in UTC
        @return the date/time in database format
        @see dateTimeToQDateTime
     */
    static QString dateTimeFromQDateTime( const QDateTime & dateTime );

    /** Converts the given date/time from database format to QDateTime.
        @param dateTime the date/time in database format
        @return the date/time as QDateTime
        @see dateTimeFromQDateTime
     */
    static QDateTime dateTimeToQDateTime( const QByteArray & dateTime );

    /**
      Returns the D-Bus interface of the given resource.
    */
    OrgKdeAkonadiResourceInterface* resourceInterface( const Resource &res );

private:
    QString m_connectionName;
    QSqlDatabase m_database;
    bool m_dbOpened;
    bool m_inTransaction;
    QByteArray mSessionId;
    NotificationCollector* mNotificationCollector;

    static QList<int> mPendingItemDeliveries;
    static QMutex mPendingItemDeliveriesMutex;
    static QWaitCondition mPendingItemDeliveriesCondition;

    // database configuration
    static QString mDbDriverName;
    static QString mDbName;
    static QString mDbHostName;
    static QString mDbUserName;
    static QString mDbPassword;
    static QString mDbConnectionOptions;

    // resource dbus interface cache
    QHash<int, OrgKdeAkonadiResourceInterface*> mResourceInterfaceCache;
};

}
#endif
