/*
    Copyright (c) 2007 Tobias Koenig <tokoe@kde.org>

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

#ifndef VCARDRESOURCE_H
#define VCARDRESOURCE_H

#include "singlefileresource.h"
#include "settings.h"

#include <kcontacts/addressee.h>
#include <kcontacts/vcardconverter.h>

class VCardResource : public Akonadi::SingleFileResource<Akonadi_VCard_Resource::Settings>
{
    Q_OBJECT

public:
    explicit VCardResource(const QString &id);
    ~VCardResource();

protected Q_SLOTS:
    bool retrieveItem(const Akonadi::Item &item, const QSet<QByteArray> &parts);
    void retrieveItems(const Akonadi::Collection &col);

protected:
    /**
     * Customize the configuration dialog before it is displayed.
     */
    virtual void customizeConfigDialog(Akonadi::SingleFileResourceConfigDialog<Akonadi_VCard_Resource::Settings> *dlg);

    bool readFromFile(const QString &fileName);
    bool writeToFile(const QString &fileName);
    virtual void aboutToQuit();

    virtual void itemAdded(const Akonadi::Item &item, const Akonadi::Collection &collection);
    virtual void itemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts);
    virtual void itemRemoved(const Akonadi::Item &item);

private:
    QMap<QString, KContacts::Addressee> mAddressees;
    KContacts::VCardConverter mConverter;
};

#endif
