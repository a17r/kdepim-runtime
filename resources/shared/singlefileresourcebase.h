/*
    Copyright (c) 2008 Volker Krause <vkrause@kde.org>

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

#ifndef AKONADI_SINGLEFILERESOURCEBASE_H
#define AKONADI_SINGLEFILERESOURCEBASE_H

#include <akonadi/resourcebase.h>

#include <KDE/KUrl>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

namespace Akonadi
{

/**
 * Base class for single file based resources.
 * @see SingleFileResource
 */
class SingleFileResourceBase : public ResourceBase, public Akonadi::AgentBase::Observer
{
  Q_OBJECT
  public:
    SingleFileResourceBase( const QString &id );

    /**
     * Set the mimetypes supported by this resource and an optional icon for the collection.
     */
    void setSupportedMimetypes( const QStringList &mimeTypes, const QString &icon = QString() );

    void collectionChanged( const Akonadi::Collection &collection );

  public Q_SLOTS:
    void reloadFile();
    virtual void readFile() = 0;
    virtual void writeFile() = 0;

  protected:
    /**
     * Reimplement to read your data from the given file.
     * The file is always local, loading from the network is done
     * automatically if needed.
     */
    virtual bool readFromFile( const QString &fileName ) = 0;

    /**
     * Reimplement to write your data to the given file.
     * The file is always local, storing back to the network url is done
     * automatically when needed.
     */
    virtual bool writeToFile( const QString &fileName ) = 0;

  protected:
    QTimer mDirtyTimer;
    KUrl mCurrentUrl;
    QStringList mSupportedMimetypes;
    QString mCollectionIcon;

  private Q_SLOTS:
    void fileChanged( const QString &fileName );
};

}

#endif
