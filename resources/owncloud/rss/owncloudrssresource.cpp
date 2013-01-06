/*
    Copyright (C) 2012    Frank Osterfeld <osterfeld@kde.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "owncloudrssresource.h"
#include "settings.h"
#include "settingsadaptor.h"
#include "configdialog.h"
#include "jobs.h"

#include <QtDBus/QDBusConnection>

#include <KWindowSystem>

#include <Akonadi/EntityDisplayAttribute>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ChangeRecorder>
#include <Akonadi/Collection>
#include <Akonadi/CollectionModifyJob>
#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/AttributeFactory>

#include <KWallet/Wallet>

#include <KRss/Item>

using namespace Akonadi;
using namespace boost;
using namespace KWallet;

static QString mimeType() {
    return QLatin1String("application/rss+xml");
}

OwncloudRssResource::OwncloudRssResource( const QString &id )
    : ResourceBase( id )
    , m_wallet( Wallet::openWallet( Wallet::NetworkWallet(), 0, Wallet::Asynchronous ) )
    , m_walletOpenedReceived( false )
{
    Q_ASSERT( m_wallet );
    connect( m_wallet, SIGNAL(walletOpened(bool)), this, SLOT(walletOpened(bool)) );
    QDBusConnection::sessionBus().registerObject( QLatin1String( "/Settings" ),
                            Settings::self(), QDBusConnection::ExportAdaptors );

#if 0
    m_policy.setInheritFromParent( false );
    m_policy.setSyncOnDemand( false );
    m_policy.setLocalParts( QStringList() << KRss::Item::HeadersPart << KRss::Item::ContentPart << Akonadi::Item::FullPayload );

    // Change recording makes the resource unusable for hours here
    // after migrating 130000 items, so disable it, as we don't write back item changes anyway.
    changeRecorder()->setChangeRecordingEnabled( false );
    changeRecorder()->fetchCollection( true );
    changeRecorder()->fetchChangedOnly( true );
    changeRecorder()->setItemFetchScope( ItemFetchScope() );
#endif
}

OwncloudRssResource::~OwncloudRssResource()
{
    delete m_wallet;
}

static const QString walletFolderName = QLatin1String("Akonadi Owncloud");

static QString getPassword( Wallet* wallet, const QString& identifier ) {
    if ( !wallet->hasFolder( walletFolderName ) )
        return QString();

    if ( !wallet->setFolder( walletFolderName ) )
        return QString();

    QString password;
    const int ret = wallet->readPassword( identifier, /*out*/ password );
    if ( ret == 0 )
        return password;
    else
        return QString();
}

static void writePassword( Wallet* wallet, const QString& identifier, const QString& password )
{
    //report errors?
    if ( !wallet->isOpen() )
        return;
    if ( !wallet->hasFolder( walletFolderName ) ) {
        if ( !wallet->createFolder( walletFolderName ) )
            return;
    }

    if ( !wallet->setFolder( walletFolderName ) )
        return;

    const int ret = wallet->writePassword( identifier, password );
    if ( ret != 0 )
        return;
}

void OwncloudRssResource::walletOpened( bool success )
{
    m_walletOpenedReceived = true;

    if ( success )
        m_password = getPassword( m_wallet, identifier() );
    else
        m_password.clear();

    Q_FOREACH( ::Job* const job, m_pendingJobsWaitingForWallet ) {
        job->setPassword( m_password );
        job->start();
    }
    m_pendingJobsWaitingForWallet.clear();

    Q_FOREACH( const WId windowId, m_configDialogsWaitingForWallet )
        reallyConfigure( windowId );
    m_configDialogsWaitingForWallet.clear();
}

void OwncloudRssResource::waitForWalletAndStart( ::Job* job ) {
    if ( m_walletOpenedReceived ) {
        Q_ASSERT( m_pendingJobsWaitingForWallet.isEmpty() );
        job->setPassword( m_password );
        job->start();
        return;
    } else {
        m_pendingJobsWaitingForWallet.append( job );
    }
}

void OwncloudRssResource::nodesListed( KJob* j )
{
    ListNodeJob* job = qobject_cast<ListNodeJob*>( j );
    Q_ASSERT( job );

    if ( job->error() != KJob::NoError ) {
        error( job->errorString() );
        return;
    }

    Collection::List collections;

    // create a top-level collection
    Collection top;
    top.setParent( Collection::root() );
    top.setRemoteId( QLatin1String("/") );
    top.setContentMimeTypes( QStringList() << Collection::mimeType() << mimeType() );
    top.setName( i18n("Owncloud News") );
    top.setRights( Collection::CanCreateCollection );
    top.attribute<Akonadi::EntityDisplayAttribute>( Collection::AddIfMissing )->setDisplayName( i18n("Owncloud News") );
    top.attribute<Akonadi::EntityDisplayAttribute>( Collection::AddIfMissing )->setIconName( QString("application-opml+xml") );
    collections << top;

    QVector<ListNodeJob::Node> nodes = job->children();
    Q_FOREACH( const ListNodeJob::Node& i, nodes ) {
        Collection child;
        child.setParent( top );
        child.setRemoteId( i.id );
        child.setContentMimeTypes( QStringList() << mimeType() );
        child.setName( i.id );
        child.attribute<Akonadi::EntityDisplayAttribute>( Collection::AddIfMissing )->setDisplayName( i.title );
        child.attribute<Akonadi::EntityDisplayAttribute>( Collection::AddIfMissing )->setIconName( QString("application-opml+xml") );
        collections.append( child );
    }

    collectionsRetrieved( collections );
}

void OwncloudRssResource::retrieveCollections()
{
    if ( m_listJob )
        return;
    ListNodeJob* job = new ListNodeJob( this );
    job->setUrl( Settings::owncloudServerUrl() );
    job->setUsername( Settings::username() );
    connect( job, SIGNAL(result(KJob*)), this, SLOT(nodesListed(KJob*)) );
    waitForWalletAndStart( job );
    m_listJob = job;
}


void OwncloudRssResource::retrieveItems( const Akonadi::Collection &collection )
{
    itemsRetrieved( Akonadi::Item::List() );
}


bool OwncloudRssResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray> &parts )
{
    Q_UNUSED( parts );

    itemRetrieved( item );
    return true;
}

void OwncloudRssResource::reallyConfigure(WId windowId)
{
    QPointer<ConfigDialog> dlg( new ConfigDialog( identifier() ) );
    dlg->setPassword( m_password );
    if ( windowId )
        KWindowSystem::setMainWindow( dlg, windowId );
    if ( dlg->exec() == KDialog::Accepted ) {
        m_password = dlg->password();
        writePassword( m_wallet, identifier(), m_password );
        emit configurationDialogAccepted();
    } else {
        emit configurationDialogRejected();
    }
    delete dlg;

    Settings::self()->writeConfig();

    synchronizeCollectionTree();
}

void OwncloudRssResource::configure( WId windowId )
{
    if ( m_walletOpenedReceived )
        reallyConfigure( windowId );
    else
        m_configDialogsWaitingForWallet.append( windowId );

}

void OwncloudRssResource::collectionChanged(const Akonadi::Collection& collection)
{
    changeCommitted( collection );
}

void OwncloudRssResource::collectionAdded( const Collection &collection, const Collection &parent )
{
    Q_UNUSED( parent )
    changeCommitted( collection );
}

void OwncloudRssResource::collectionRemoved( const Collection &collection )
{
    changeCommitted( collection );
}


AKONADI_RESOURCE_MAIN( OwncloudRssResource )

#include "owncloudrssresource.moc"