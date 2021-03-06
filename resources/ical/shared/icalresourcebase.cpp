/*
    Copyright (c) 2006 Till Adam <adam@kde.org>
    Copyright (c) 2009 David Jarvie <djarvie@kde.org>

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

#include "icalresourcebase.h"
#include "icalsettingsadaptor.h"
#include "singlefileresourceconfigdialog.h"

#include <kdbusconnectionpool.h>

#include <KCalCore/FileStorage>
#include <KCalCore/MemoryCalendar>
#include <KCalCore/Incidence>
#include <KCalCore/ICalFormat>

#include <QDebug>
#include <KLocalizedString>

using namespace Akonadi;
using namespace KCalCore;
using namespace SETTINGS_NAMESPACE;

ICalResourceBase::ICalResourceBase(const QString &id)
    : SingleFileResource<Settings>(id)
{
}

void ICalResourceBase::initialise(const QStringList &mimeTypes, const QString &icon)
{
    setSupportedMimetypes(mimeTypes, icon);
    new ICalSettingsAdaptor(mSettings);
    KDBusConnectionPool::threadConnection().registerObject(QStringLiteral("/Settings"),
            mSettings, QDBusConnection::ExportAdaptors);
}

ICalResourceBase::~ICalResourceBase()
{
}

bool ICalResourceBase::retrieveItem(const Akonadi::Item &item,
                                    const QSet<QByteArray> &parts)
{
    qDebug() << "Item:" << item.url();

    if (!mCalendar) {
        qCritical() << "akonadi_ical_resource: Calendar not loaded";
        Q_EMIT error(i18n("Calendar not loaded."));
        return false;
    }

    return doRetrieveItem(item, parts);
}

void ICalResourceBase::aboutToQuit()
{
    if (!mSettings->readOnly()) {
        writeFile();
    }
    mSettings->save();
}

void ICalResourceBase::customizeConfigDialog(SingleFileResourceConfigDialog<Settings> *dlg)
{
    dlg->setFilter(QStringLiteral("text/calendar"));
    dlg->setWindowTitle(i18n("Select Calendar"));
}

bool ICalResourceBase::readFromFile(const QString &fileName)
{
    mCalendar = KCalCore::MemoryCalendar::Ptr(new KCalCore::MemoryCalendar(QStringLiteral("UTC")));
    mFileStorage = KCalCore::FileStorage::Ptr(new KCalCore::FileStorage(mCalendar, fileName,
                   new KCalCore::ICalFormat()));
    const bool result = mFileStorage->load();
    if (!result) {
        qCritical() << "akonadi_ical_resource: Error loading file " << fileName;
    }

    return result;
}

void ICalResourceBase::itemRemoved(const Akonadi::Item &item)
{
    if (!mCalendar) {
        qCritical() << "akonadi_ical_resource: mCalendar is 0!";
        cancelTask(i18n("Calendar not loaded."));
        return;
    }

    Incidence::Ptr i = mCalendar->instance(item.remoteId());
    if (i) {
        if (!mCalendar->deleteIncidence(i)) {
            qCritical() << "akonadi_ical_resource: Can't delete incidence with instance identifier "
                        << item.remoteId() << "; item.id() = " << item.id();
            cancelTask();
            return;
        }
    } else {
        qCritical() << "akonadi_ical_resource: itemRemoved(): Can't find incidence with instance identifier "
                    << item.remoteId() << "; item.id() = " << item.id();
    }
    scheduleWrite();
    changeProcessed();
}

void ICalResourceBase::retrieveItems(const Akonadi::Collection &col)
{
    reloadFile();
    if (mCalendar) {
        doRetrieveItems(col);
    } else {
        qCritical() << "akonadi_ical_resource: retrieveItems(): mCalendar is 0!";
    }
}

bool ICalResourceBase::writeToFile(const QString &fileName)
{
    if (!mCalendar) {
        qCritical() << "akonadi_ical_resource: writeToFile() mCalendar is 0!";
        return false;
    }

    KCalCore::FileStorage *fileStorage = mFileStorage.data();
    if (fileName != mFileStorage->fileName()) {
        fileStorage = new KCalCore::FileStorage(mCalendar,
                                                fileName,
                                                new KCalCore::ICalFormat());
    }

    bool success = true;
    if (!fileStorage->save()) {
        qCritical() << QStringLiteral("akonadi_ical_resource: Failed to save calendar to file ") + fileName;
        Q_EMIT error(i18n("Failed to save calendar file to %1", fileName));
        success = false;
    }

    if (fileStorage != mFileStorage.data()) {
        delete fileStorage;
    }

    return success;
}

KCalCore::MemoryCalendar::Ptr ICalResourceBase::calendar() const
{
    return mCalendar;
}

KCalCore::FileStorage::Ptr ICalResourceBase::fileStorage() const
{
    return mFileStorage;
}

