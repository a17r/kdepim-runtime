/*
    Copyright (c) 2008 Tobias Koenig <tokoe@kde.org>

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

#ifndef VCARDDIRRESOURCE_H
#define VCARDDIRRESOURCE_H

#include <resourcebase.h>

#include <kcontacts/addressee.h>
#include <kcontacts/vcardconverter.h>

class VCardDirResource : public Akonadi::ResourceBase, public Akonadi::AgentBase::Observer
{
    Q_OBJECT

public:
    VCardDirResource(const QString &id);
    ~VCardDirResource();

public Q_SLOTS:
    void configure(WId windowId) Q_DECL_OVERRIDE;
    void aboutToQuit() Q_DECL_OVERRIDE;

protected:
    using ResourceBase::retrieveItems; // Suppress -Woverload-virtual

protected Q_SLOTS:
    void retrieveCollections() Q_DECL_OVERRIDE;
    void retrieveItems(const Akonadi::Collection &col) Q_DECL_OVERRIDE;
    bool retrieveItem(const Akonadi::Item &item, const QSet<QByteArray> &parts) Q_DECL_OVERRIDE;

protected:
    void itemAdded(const Akonadi::Item &item, const Akonadi::Collection &collection) Q_DECL_OVERRIDE;
    void itemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts) Q_DECL_OVERRIDE;
    void itemRemoved(const Akonadi::Item &item) Q_DECL_OVERRIDE;

private:
    bool loadAddressees();
    QString vCardDirectoryName() const;
    QString vCardDirectoryFileName(const QString &file) const;
    void initializeVCardDirectory() const;

private:
    QMap<QString, KContacts::Addressee> mAddressees;
    KContacts::VCardConverter mConverter;
};

#endif
