/*
    Copyright (C) 2011  Christian Mollekopf <chrigi_1@fastmail.fm>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "feederqueue.h"

#include <nie.h>

#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/Collection>

#include <KLocalizedString>
#include <KUrl>
#include <KJob>
#include <KIcon>
#include <KNotification>
#include <KIconLoader>
#include <KStandardDirs>

#include <QDateTime>

#include "nepomukhelpers.h"
#include "nepomukfeeder-config.h"

using namespace Akonadi;

FeederQueue::FeederQueue( bool persistQueue, QObject* parent )
: QObject( parent ),
  mTotalAmount( 0 ),
  mProcessedAmount( 0 ),
  mPendingJobs( 0 ),
  mReIndex( false ),
  mOnline( true ),
  lowPrioQueue(1, 100, this),
  highPrioQueue(1, 100, this),
  unindexedItemQueue(1, 100, this)
{
  if (persistQueue) {
    lowPrioQueue.setSaveFile(KStandardDirs::locateLocal("data", QLatin1String("akonadi_nepomuk_feeder/lowPrioQueue"), true));
    highPrioQueue.setSaveFile(KStandardDirs::locateLocal("data", QLatin1String("akonadi_nepomuk_feeder/highPrioQueue"), true));
  }
  unindexedItemQueue.setItemsAreNotIndexed( true );
  mProcessItemQueueTimer.setInterval( 0 );
  mProcessItemQueueTimer.setSingleShot( true );
  connect( &mProcessItemQueueTimer, SIGNAL(timeout()), SLOT(processItemQueue()) );

  connect( &lowPrioQueue, SIGNAL(finished()), SLOT(prioQueueFinished()));
  connect( &highPrioQueue, SIGNAL(finished()), SLOT(prioQueueFinished()));
  connect( &unindexedItemQueue, SIGNAL(finished()), SLOT(prioQueueFinished()));
  connect( &lowPrioQueue, SIGNAL(batchFinished()), SLOT(batchFinished()));
  connect( &highPrioQueue, SIGNAL(batchFinished()), SLOT(batchFinished()));
  connect( &unindexedItemQueue, SIGNAL(batchFinished()), SLOT(batchFinished()));
}

FeederQueue::~FeederQueue()
{

}

void FeederQueue::setReindexing( bool reindex )
{
  mReIndex = reindex;
}

void FeederQueue::setOnline( bool online )
{
  //kDebug() << online;
  mOnline = online;
  if ( online )
      continueIndexing();
}

void FeederQueue::setIndexingSpeed(FeederQueue::IndexingSpeed speed)
{
    const int s_reducedSpeedDelay = 500; // ms
    const int s_snailPaceDelay = 3000;   // ms

    kDebug() << speed;

    //
    // The low prio queue is always throttled a little more than the high prio one
    //
    if ( speed == FullSpeed ) {
        lowPrioQueue.setProcessingDelay( 0 );
        highPrioQueue.setProcessingDelay( 0 );
        unindexedItemQueue.setProcessingDelay( 0 );
    } else {
        lowPrioQueue.setProcessingDelay( s_snailPaceDelay );
        highPrioQueue.setProcessingDelay( s_reducedSpeedDelay );
        unindexedItemQueue.setProcessingDelay( s_snailPaceDelay );
    }
}

void FeederQueue::addCollection( const Akonadi::Collection &collection )
{
  //kDebug() << collection.id();

  // If the collection contains mail, append it, otherwise prepend.
  // This ensures the smaller, fewer collections with things like
  // contacts or events in them are processed first. They tend to
  // be more important than the full text index of mail.
  // Bit of a hack, yes, would probably be better to have priorties
  // in the plugins and then keep a priority queue properly.
  if(mCollectionQueue.contains(collection)) {
    return;
  }
  if ( collection.contentMimeTypes().contains( QLatin1String( "message/rfc822" ) ) )
    mCollectionQueue.append( collection );
  else
    mCollectionQueue.prepend( collection );

  if ( mPendingJobs == 0 ) {
    processNextCollection();
  }
}

void FeederQueue::processNextCollection()
{
  //kDebug();
  if ( mCurrentCollection.isValid() )
    return;
  mTotalAmount = 0;
  mProcessedAmount = 0;
  if ( mCollectionQueue.isEmpty() ) {
    indexingComplete();
    return;
  }
  mCurrentCollection = mCollectionQueue.takeFirst();
  emit running( i18n( "Indexing collection '%1'...", mCurrentCollection.name() ) );
  kDebug() << "Indexing collection " << mCurrentCollection.name() << mCurrentCollection.id();

  // process the collection only if it has not already been indexed
  if ( !mReIndex && NepomukHelpers::isIndexed(mCurrentCollection) ) {
    kDebug() << "already indexed collection: " << mCurrentCollection.id() << " skipping";
    mCurrentCollection = Collection();
    QTimer::singleShot(0, this, SLOT(processNextCollection()));
    return;
  }
  KJob *job = NepomukHelpers::addCollectionToNepomuk( mCurrentCollection );
  connect( job, SIGNAL(result(KJob*)), this, SLOT(jobResult(KJob*)));

  ItemFetchJob *itemFetch = new ItemFetchJob( mCurrentCollection, this );
  itemFetch->fetchScope().setCacheOnly( true );
  connect( itemFetch, SIGNAL(finished(KJob*)), SLOT(itemFetchResult(KJob*)) );
  ++mPendingJobs;
}

void FeederQueue::itemHeadersReceived( const Akonadi::Item::List& items )
{
  kDebug() << items.count();
  Akonadi::Item::List itemsToUpdate;
  foreach ( const Item &item, items ) {
    if ( item.storageCollectionId() != mCurrentCollection.id() )
      continue; // stay away from links

    // update item if it does not exist or does not have a proper id
    if ( mReIndex || !NepomukHelpers::isIndexed(item)) {
      itemsToUpdate.append( item );
    }
  }

  if ( !itemsToUpdate.isEmpty() ) {
    lowPrioQueue.addItems( itemsToUpdate );
    mTotalAmount += itemsToUpdate.size();
    mProcessItemQueueTimer.start();
  }
}

void FeederQueue::itemFetchResult(KJob* job)
{
  if ( job->error() )
    kWarning() << job->errorString();

  Akonadi::ItemFetchJob *fetchJob = qobject_cast<Akonadi::ItemFetchJob *>( job );
  Q_ASSERT( fetchJob );
  itemHeadersReceived( fetchJob->items() );

  --mPendingJobs;
  if ( mPendingJobs == 0 && lowPrioQueue.isEmpty() ) { //Fetch jobs finished but there were no items in the collection
    collectionFullyIndexed();
    return;
  }
}

void FeederQueue::addItem( const Akonadi::Item &item )
{
  kDebug() << item.id();
  highPrioQueue.addItem( item );
  mProcessItemQueueTimer.start();
}

void FeederQueue::addUnindexedItem(const Item &item )
{
  kDebug() << item.id();
  unindexedItemQueue.addItem( item );
  mProcessItemQueueTimer.start();
}

bool FeederQueue::isEmpty()
{
  return allQueuesEmpty() && mCollectionQueue.isEmpty();
}

void FeederQueue::continueIndexing()
{
  kDebug();
  mProcessItemQueueTimer.start();
}

void FeederQueue::collectionFullyIndexed()
{
    NepomukHelpers::markCollectionAsIndexed( mCurrentCollection );
    const QString summary = i18n( "Indexing collection '%1' completed.", mCurrentCollection.name() );
    mCurrentCollection = Collection();
    emit idle( i18n( "Indexing completed." ) );
    const QPixmap pixmap = KIcon( "nepomuk" ).pixmap( KIconLoader::SizeSmall, KIconLoader::SizeSmall );
    KNotification::event( QLatin1String("indexingcollectioncompleted"),
                            summary,
                            pixmap,
                            0,
                            KNotification::CloseOnTimeout,
                            KGlobal::mainComponent());


    //kDebug() << "indexing of collection " << mCurrentCollection.id() << " completed";
    processNextCollection();
}

void FeederQueue::indexingComplete()
{
  //kDebug() << "fully indexed";
  mReIndex = false;
  emit fullyIndexed();
}

void FeederQueue::processItemQueue()
{
  //kDebug();
  ++mProcessedAmount;
  if ( ( mProcessedAmount % 100 ) == 0 && mTotalAmount > 0 && mProcessedAmount <= mTotalAmount )
    emit progress( ( mProcessedAmount * 100 ) / mTotalAmount );

  if ( !mOnline ) {
    kDebug() << "not Online, stopping processing";
    return;
  } else if ( !highPrioQueue.isEmpty() ) {
    //kDebug() << "high";
    if ( !highPrioQueue.processItem() ) {
      return;
    }
  } else if ( !lowPrioQueue.isEmpty() ) {
    //kDebug() << "low";
    if ( !lowPrioQueue.processItem() ) {
      return;
    }
  } else if ( !unindexedItemQueue.isEmpty() ) {
    if ( !unindexedItemQueue.processItem() ) {
      return;
    }
  } else {
    //kDebug() << "idle";
    emit idle( i18n( "Ready to index data." ) );
  }

  if ( !allQueuesEmpty() ) {
    //kDebug() << "continue";
    // go to eventloop before processing the next one, otherwise we miss the idle status change
    mProcessItemQueueTimer.start();
  }
}

void FeederQueue::prioQueueFinished()
{
  if ( allQueuesEmpty() && ( mPendingJobs == 0 )) {
    if (mCurrentCollection.isValid()) {
      collectionFullyIndexed();
    } else {
      indexingComplete();
    }
  }
}

bool FeederQueue::allQueuesEmpty() const
{
  if ( highPrioQueue.isEmpty() && lowPrioQueue.isEmpty() && unindexedItemQueue.isEmpty() ) {
    return true;
  }
  return false;
}


void FeederQueue::batchFinished()
{
  /*if ( sender() == &highPrioQueue )
    kDebug() << "high prio batch finished--------------------";
  if ( sender() == &lowPrioQueue )
    kDebug() << "low prio batch finished--------------------";*/
  if ( !allQueuesEmpty() ) {
    //kDebug() << "continue";
    // go to eventloop before processing the next one, otherwise we miss the idle status change
    mProcessItemQueueTimer.start();
  }
}

void FeederQueue::jobResult(KJob* job)
{
  if ( job->error() )
    kWarning() << job->errorString();
}

const Akonadi::Collection& FeederQueue::currentCollection()
{
  return mCurrentCollection;
}

Akonadi::Collection::List FeederQueue::listOfCollection() const
{
  return mCollectionQueue;
}

#include "feederqueue.moc"
