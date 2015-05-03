/*
   Copyright (C) 2011-2013 Daniel Vrátil <dvratil@redhat.com>

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

#include "googleresource.h"
#include "googlesettings.h"

#include <AkonadiCore/ChangeRecorder>
#include <AkonadiCore/ItemFetchScope>
#include <AkonadiCore/CollectionFetchScope>

#include <QDialog>
#include <KLocalizedString>

#include <KGAPI/Account>
#include <KGAPI/AccountInfoFetchJob>
#include <KGAPI/AccountInfo>
#include <KGAPI/AuthJob>

#ifdef HAVE_ACCOUNTS
#include "../../shared/singlefileresource/getcredentialsjob.h"
#endif

#define ACCESS_TOKEN_PROPERTY "AccessToken"

Q_DECLARE_METATYPE(KGAPI2::Job *)

using namespace KGAPI2;
using namespace Akonadi;

GoogleResource::GoogleResource(const QString &id):
    ResourceBase(id),
    AgentBase::ObserverV2(),
    m_isConfiguring(false)
{
    //QT5 KLocalizedString::global()->insertCatalog( QStringLiteral("akonadi_google_resource") );
    connect(this, &GoogleResource::abortRequested, this, &GoogleResource::slotAbortRequested);
    connect(this, &GoogleResource::reloadConfiguration, this, &GoogleResource::reloadConfig);

    setNeedsNetwork(true);

    changeRecorder()->itemFetchScope().fetchFullPayload(true);
    changeRecorder()->itemFetchScope().setAncestorRetrieval(ItemFetchScope::All);
    changeRecorder()->fetchCollection(true);
    changeRecorder()->collectionFetchScope().setAncestorRetrieval(CollectionFetchScope::All);

    m_accountMgr = new GoogleAccountManager(this);
    connect(m_accountMgr, &GoogleAccountManager::accountChanged, this, &GoogleResource::slotAccountChanged);
    connect(m_accountMgr, &GoogleAccountManager::accountRemoved, this, &GoogleResource::slotAccountRemoved);
    connect(m_accountMgr, &GoogleAccountManager::managerReady, this, &GoogleResource::slotAccountManagerReady);

    Q_EMIT status(NotConfigured, i18n("Waiting for KWallet..."));
}

GoogleResource::~GoogleResource()
{
}

void GoogleResource::cleanup()
{
    accountManager()->cleanup(settings()->account());
}

AccountPtr GoogleResource::account() const
{
    return m_account;
}

GoogleAccountManager *GoogleResource::accountManager() const
{
    return m_accountMgr;
}

void GoogleResource::aboutToQuit()
{
    slotAbortRequested();
}

void GoogleResource::abort()
{
    cancelTask(i18n("Aborted"));
}

void GoogleResource::slotAbortRequested()
{
    abort();
}

void GoogleResource::configure(WId windowId)
{
    if (!m_accountMgr->isReady() || m_isConfiguring) {
        Q_EMIT configurationDialogAccepted();
        return;
    }

    m_isConfiguring = true;
    if (runConfigurationDialog(windowId) == QDialog::Accepted) {
        updateResourceName();

        Q_EMIT configurationDialogAccepted();

        m_account = accountManager()->findAccount(settings()->account());
        if (m_account.isNull()) {
            Q_EMIT status(NotConfigured, i18n("Configured account does not exist"));
            m_isConfiguring = false;
            return;
        }

        Q_EMIT status(Idle, i18nc("@info:status", "Ready"));
        synchronize();
    } else {
        updateResourceName();

        Q_EMIT configurationDialogRejected();
    }
    m_isConfiguring = false;
}

void GoogleResource::updateAccountToken(const AccountPtr &account, KGAPI2::Job *restartJob)
{
    if (accountId() > 0) {
        configureKAccounts(accountId(), restartJob);
    } else if (!settings()->account().isEmpty()) {
        AuthJob *authJob = new AuthJob(account, settings()->clientId(), settings()->clientSecret(), this);
        authJob->setProperty(JOB_PROPERTY, QVariant::fromValue(restartJob));
        connect(authJob, &AuthJob::finished, this, &GoogleResource::slotAuthJobFinished);
    }
}

void GoogleResource::reloadConfig()
{
    const QString accountName = settings()->account();

    if (accountId() > 0) {
        if (!configureKAccounts(accountId())) {
            Q_EMIT status(Broken);
            return;
        }
    } else if (!accountName.isEmpty()) {
        if (!configureKGAPIAccount(m_accountMgr->findAccount(accountName))) {
            Q_EMIT status(NotConfigured, i18n("Configured account does not exist"));
            return;
        }
    } else {
        Q_EMIT status(NotConfigured);
        return;
    }

    Q_EMIT status(Idle, i18nc("@info:status", "Ready"));
}

bool GoogleResource::configureKAccounts(int accountId, KGAPI2::Job *restartJob)
{
    if (accountId == 0) {
        return false;
    }
#ifdef HAVE_ACCOUNTS
    GetCredentialsJob *gc = new GetCredentialsJob(accountId, this);
    gc->setProperty(JOB_PROPERTY, QVariant::fromValue(restartJob));
    connect(gc, &GetCredentialsJob::finished, this, &GoogleResource::slotKAccountsCredentialsReceived);
    gc->start();
    // SUCKS!
    return true;
#else
    Q_UNUSED(restartJob);
    return false;
#endif
}

#ifdef HAVE_ACCOUNTS
void GoogleResource::slotKAccountsCredentialsReceived(KJob *job)
{
    if (job->error()) {
        Q_EMIT status(Broken);
        // FIXME: Fallback to KGAPI account?
        return;
    }

    GetCredentialsJob *gc = qobject_cast<GetCredentialsJob *>(job);
    const QVariantMap data = gc->credentialsData();
    const QString accessToken = data.value(QStringLiteral("AccessToken")).toString();

    // Createa temporary account that we use to fetch full user name
    KGAPI2::AccountPtr account(new KGAPI2::Account);
    account->setAccessToken(accessToken);
    account->setScopes(scopes());

    KGAPI2::Job *otherJob = 0;
    if (!job->property(JOB_PROPERTY).isNull()) {
        otherJob = job->property(JOB_PROPERTY).value<KGAPI2::Job *>();
    }

    if (settings()->accountName().isEmpty()) {
        account->setAccountName(i18n("Unknown Account"));
        AccountInfoFetchJob *aiJob = new AccountInfoFetchJob(account, this);
        aiJob->setProperty(ACCESS_TOKEN_PROPERTY, accessToken);
        if (otherJob) {
            aiJob->setProperty(JOB_PROPERTY, QVariant::fromValue(otherJob));
        }
        connect(aiJob, &AccountInfoFetchJob::finished, this, &GoogleResource::slotKAccountsAccountInfoReceived);
    } else {
        m_account = AccountPtr(new Account(settings()->accountName(),
                                           accessToken));
        finishKAccountsAuthentication(otherJob);
    }
}

void GoogleResource::slotKAccountsAccountInfoReceived(KGAPI2::Job *job)
{
    if (!handleError(job)) {
        Q_EMIT error(job->errorString());
        cancelTask(i18n("Failed to refresh tokens"));
        return;
    }

    AccountInfoFetchJob *aiJob = qobject_cast<AccountInfoFetchJob *>(job);
    Q_ASSERT(aiJob);
    aiJob->deleteLater();

    const AccountPtr account = job->account();

    if (aiJob->items().count() != 1) {
        qWarning() << "AccountInfoFetchJob returned unexpected amount of results";
        Q_EMIT error(i18n("Invalid reply"));
        cancelTask(i18n("Failed to refresh tokens"));
        return;
    }

    AccountInfoPtr info = aiJob->items().first().dynamicCast<AccountInfo>();
    settings()->setAccountName(info->email());
    m_account = AccountPtr(new Account(info->email(),
                                       aiJob->property(ACCESS_TOKEN_PROPERTY).toString()));
    settings()->writeConfig();

    KGAPI2::Job *otherJob = 0;
    if (job->property(JOB_PROPERTY).isNull()) {
        otherJob = job->property(JOB_PROPERTY).value<KGAPI2::Job *>();
    }

    finishKAccountsAuthentication(otherJob);
}

void GoogleResource::finishKAccountsAuthentication(KGAPI2::Job *job)
{
    updateResourceName();
    Q_EMIT status(Idle, i18nc("@info:status", "Ready"));

    if (job) {
        job->setAccount(m_account);
        job->restart();
    } else {
        synchronize();
    }
}
#endif // HAVE_ACCOUNTS

bool GoogleResource::configureKGAPIAccount(const AccountPtr &account)
{
    m_account = account;
    return !m_account.isNull();
}

void GoogleResource::slotAccountManagerReady(bool ready)
{
    // If the resource have already been configured for KAccounts, then use that
    if (accountId() > 0) {
        return;
    }

    qDebug() << ready;
    if (!ready) {
        Q_EMIT status(Broken, i18n("Can't access KWallet"));
        return;
    }

    const QString accountName = settings()->account();
    if (accountName.isEmpty()) {
        Q_EMIT status(NotConfigured);
        return;
    }

    m_account = m_accountMgr->findAccount(accountName);
    if (m_account.isNull()) {
        Q_EMIT status(NotConfigured, i18n("Configured account does not exist"));
        return;
    }

    Q_EMIT status(Idle, i18nc("@info:status", "Ready"));
    synchronize();
}

void GoogleResource::slotAccountChanged(const AccountPtr &account)
{
    // We don't care when using KAccounts
    if (accountId() > 0) {
        return;
    }

    m_account = account;
}

void GoogleResource::slotAccountRemoved(const QString &accountName)
{
    // We don't care when using KAccounts
    if (accountId() > 0) {
        return;
    }

    if (m_account && m_account->accountName() != accountName) {
        return;
    }

    Q_EMIT status(NotConfigured, i18n("Configured account has been removed"));
    m_account.clear();
    settings()->setAccount(QString());
}

bool GoogleResource::handleError(KGAPI2::Job *job)
{
    if ((job->error() == KGAPI2::NoError) || (job->error() == KGAPI2::OK)) {
        return true;
    }

    if (job->error() == KGAPI2::Unauthorized) {
        qDebug() << job << job->errorString();

        const QList<QUrl> resourceScopes = scopes();
        Q_FOREACH (const QUrl &scope, resourceScopes) {
            if (!m_account->scopes().contains(scope)) {
                m_account->addScope(scope);
            }
        }

        updateAccountToken(m_account, job);
        return false;
    }

    cancelTask(job->errorString());
    job->deleteLater();
    return false;
}

bool GoogleResource::canPerformTask()
{
    if (!m_account && accountId() == 0) {
        cancelTask(i18nc("@info:status", "Resource is not configured"));
        Q_EMIT status(NotConfigured, i18nc("@info:status", "Resource is not configured"));
        return false;
    }

    return true;
}

void GoogleResource::slotAuthJobFinished(KGAPI2::Job *job)
{
    qDebug();

    if (job->error() != KGAPI2::NoError) {
        cancelTask(i18n("Failed to refresh tokens"));
        return;
    }

    AuthJob *authJob = qobject_cast<AuthJob *>(job);
    m_account = authJob->account();
    if (!m_accountMgr->storeAccount(m_account)) {
        qWarning() << "Failed to store account in KWallet";
    }

    KGAPI2::Job *otherJob = job->property(JOB_PROPERTY).value<KGAPI2::Job *>();
    if (otherJob) {
        otherJob->setAccount(m_account);
        otherJob->restart();
    }

    job->deleteLater();
}

void GoogleResource::slotGenericJobFinished(KGAPI2::Job *job)
{
    if (!handleError(job)) {
        return;
    }

    const Item item = job->property(ITEM_PROPERTY).value<Item>();
    const Collection collection = job->property(COLLECTION_PROPERTY).value<Collection>();
    if (item.isValid()) {
        changeCommitted(item);
    } else if (collection.isValid()) {
        changeCommitted(collection);
    } else {
        taskDone();
    }

    Q_EMIT status(Idle, i18nc("@info:status", "Ready"));

    job->deleteLater();
}

void GoogleResource::emitPercent(KGAPI2::Job *job, int processedItems, int totalItems)
{
    Q_UNUSED(job);

    Q_EMIT percent(((float) processedItems / (float) totalItems) * 100);
}

bool GoogleResource::retrieveItem(const Item &item, const QSet< QByteArray > &parts)
{
    Q_UNUSED(parts);

    /* We don't support fetching parts, the item is already fully stored. */
    itemRetrieved(item);

    return true;
}

int GoogleResource::accountId() const
{
#ifdef HAVE_ACCOUNTS
    return settings()->accountId();
#else
    return 0;
#endif
}
