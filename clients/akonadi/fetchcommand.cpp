/*
    Copyright (c) 2006 Volker Krause <volker.krause@rwth-aachen.de>

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

#include "fetchcommand.h"
#include "out.h"

#include <libakonadi/itemfetchjob.h>
#include <libakonadi/itemserializer.h>

using namespace Akonadi;

FetchCommand::FetchCommand(const QString & uid) :
    mUid( uid )
{
}

void FetchCommand::exec()
{
  DataReference ref( mUid.toInt(), QString() );
  ItemFetchJob* fetchJob = new ItemFetchJob( ref );
  if ( !fetchJob->exec() ) {
    err() << "Error fetching item '" << mUid << "': "
        << fetchJob->errorString()
        << endl;
  } else {
    foreach( Item item, fetchJob->items() ) {
      QByteArray data;
      ItemSerializer::serialize( item, Item::PartBody, data );
      out() << data << endl;
    }
  }
}
