/*
    Copyright (c) 2007 Till Adam <adam@kde.org>

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

#ifndef __AKONADI_SERIALIZER_ADDRESSEE_H__
#define __AKONADI_SERIALIZER_ADDRESSEE_H__

#include <QtCore/QObject>

#include <AkonadiCore/differencesalgorithminterface.h>
#include <AkonadiCore/itemserializerplugin.h>
#include <AkonadiCore/gidextractorinterface.h>
#include <kcontacts/vcardconverter.h>

namespace Akonadi
{

class SerializerPluginAddressee : public QObject,
    public ItemSerializerPlugin,
    public DifferencesAlgorithmInterface,
    public GidExtractorInterface
{
    Q_OBJECT
    Q_INTERFACES(Akonadi::ItemSerializerPlugin)
    Q_INTERFACES(Akonadi::DifferencesAlgorithmInterface)
    Q_INTERFACES(Akonadi::GidExtractorInterface)
    Q_PLUGIN_METADATA(IID "org.kde.akonadi.SerializerPluginAddressee")
public:
    bool deserialize(Item &item, const QByteArray &label, QIODevice &data, int version) Q_DECL_OVERRIDE;
    void serialize(const Item &item, const QByteArray &label, QIODevice &data, int &version) Q_DECL_OVERRIDE;

    void compare(Akonadi::AbstractDifferencesReporter *reporter,
                 const Akonadi::Item &leftItem,
                 const Akonadi::Item &rightItem) Q_DECL_OVERRIDE;

    QString extractGid(const Item &item) const Q_DECL_OVERRIDE;

private:
    KContacts::VCardConverter m_converter;
};

}

#endif
