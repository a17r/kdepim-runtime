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

#ifndef MAILDIR_RESOURCE_H
#define MAILDIR_RESOURCE_H

#include <collection.h>
#include <resourcebase.h>

class QTimer;
class QFileInfo;
class KDirWatch;
namespace Akonadi_Maildir_Resource
{
class MaildirSettings;
}
namespace KPIM
{
class Maildir;
}

class MaildirResource : public Akonadi::ResourceBase, public Akonadi::AgentBase::ObserverV2
{
    Q_OBJECT

public:
    MaildirResource(const QString &id);
    ~MaildirResource();

    virtual QString defaultResourceType();
public Q_SLOTS:
    void configure(WId windowId) Q_DECL_OVERRIDE;

protected Q_SLOTS:
    void retrieveCollections() Q_DECL_OVERRIDE;
    void retrieveItems(const Akonadi::Collection &col) Q_DECL_OVERRIDE;
    bool retrieveItem(const Akonadi::Item &item, const QSet<QByteArray> &parts) Q_DECL_OVERRIDE;

protected:
    virtual QString itemMimeType() const;

    void aboutToQuit() Q_DECL_OVERRIDE;

    void itemAdded(const Akonadi::Item &item, const Akonadi::Collection &collection) Q_DECL_OVERRIDE;
    void itemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts) Q_DECL_OVERRIDE;
    void itemMoved(const Akonadi::Item &item, const Akonadi::Collection &source, const Akonadi::Collection &dest) Q_DECL_OVERRIDE;
    void itemRemoved(const Akonadi::Item &item) Q_DECL_OVERRIDE;

    void collectionAdded(const Akonadi::Collection &collection, const Akonadi::Collection &parent) Q_DECL_OVERRIDE;
    void collectionChanged(const Akonadi::Collection &collection) Q_DECL_OVERRIDE;
    // do not hide the other variant, use implementation from base class
    // which just forwards to the one above
    using Akonadi::AgentBase::ObserverV2::collectionChanged;
    void collectionMoved(const Akonadi::Collection &collection, const Akonadi::Collection &source, const Akonadi::Collection &dest) Q_DECL_OVERRIDE;
    void collectionRemoved(const Akonadi::Collection &collection) Q_DECL_OVERRIDE;

private slots:
    void configurationChanged();
    void slotItemsRetrievalResult(KJob *job);
    void slotDirChanged(const QString &dir);
    void slotFileChanged(const QFileInfo &fileInfo);
    void fsWatchDirFetchResult(KJob *job);
    void fsWatchFileFetchResult(KJob *job);
    void fsWatchFileModifyResult(KJob *job);
    // Try to restore some config values from Akonadi data
    void attemptConfigRestoring(KJob *job);
    void changedCleaner();

private:
    bool ensureDirExists();
    bool ensureSaneConfiguration();
    Akonadi::Collection::List listRecursive(const Akonadi::Collection &root, const KPIM::Maildir &dir);
    /** Creates a maildir object for the collection @p col, given it has the full ancestor chain set. */
    KPIM::Maildir maildirForCollection(const Akonadi::Collection &col);
    /** Creates a collection object for the given maildir @p md. */
    Akonadi::Collection collectionForMaildir(const KPIM::Maildir &md) const;

    QString maildirPathForCollection(const Akonadi::Collection &collection) const;
    void stopMaildirScan(const KPIM::Maildir &maildir);
    void restartMaildirScan(const KPIM::Maildir &maildir);

private:
    Akonadi_Maildir_Resource::MaildirSettings *mSettings;
    KDirWatch *mFsWatcher;
    QHash<QString, KPIM::Maildir> mMaildirsForCollection;
    QSet<QString> mChangedFiles; //files changed by the resource and that should be ignored in slotFileChanged
    QTimer *mChangedCleanerTimer;
};

#endif
