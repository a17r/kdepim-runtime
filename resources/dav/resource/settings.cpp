/*
    Copyright (c) 2009 Grégory Oestreicher <greg@kamago.net>
      Based on an original work for the IMAP resource which is :
    Copyright (c) 2008 Volker Krause <vkrause@kde.org>
    Copyright (c) 2008 Omat Holding B.V. <info@omat.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "settings.h"

#include "settingsadaptor.h"
#include "utils.h"
#include "davresource_debug.h"

#include <kapplication.h>
#include <klineedit.h>
#include <KLocalizedString>

#include <kwallet.h>

#include <KDAV/Utils>

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QRegExp>
#include <QtCore/QUrl>
#include <QtDBus/QDBusConnection>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QPushButton>

class SettingsHelper
{
public:
    SettingsHelper()
        : q(nullptr)
    {
    }

    ~SettingsHelper()
    {
        delete q;
    }

    Settings *q;
};

Q_GLOBAL_STATIC(SettingsHelper, s_globalSettings)

Settings::UrlConfiguration::UrlConfiguration()
{
}

Settings::UrlConfiguration::UrlConfiguration(const QString &serialized)
{
    const QStringList splitString = serialized.split(QLatin1Char('|'));

    if (splitString.size() == 3) {
        mUrl = splitString.at(2);
        mProtocol = KDAV::Utils::protocolByName(splitString.at(1));
        mUser = splitString.at(0);
    }
}

QString Settings::UrlConfiguration::serialize()
{
    QString serialized = mUser;
    serialized.append(QLatin1Char('|')).append(KDAV::Utils::protocolName(KDAV::Protocol(mProtocol)));
    serialized.append(QLatin1Char('|')).append(mUrl);
    return serialized;
}

Settings *Settings::self()
{
    if (!s_globalSettings->q) {
        new Settings;
        s_globalSettings->q->load();
    }

    return s_globalSettings->q;
}

Settings::Settings()
    : SettingsBase(), mWinId(0)
{

    Q_ASSERT(!s_globalSettings->q);
    s_globalSettings->q = this;

    new SettingsAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Settings"), this,
            QDBusConnection::ExportAdaptors | QDBusConnection::ExportScriptableContents);

    if (settingsVersion() == 1) {
        updateToV2();
    }

    if (settingsVersion() == 2) {
        updateToV3();
    }
}

Settings::~Settings()
{
    QMapIterator<QString, UrlConfiguration *> it(mUrls);
    while (it.hasNext()) {
        it.next();
        delete it.value();
    }
}

void Settings::setWinId(WId winId)
{
    mWinId = winId;
}

void Settings::cleanup()
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), mWinId);
    if (wallet && wallet->isOpen()) {
        if (wallet->hasFolder(KWallet::Wallet::PasswordFolder())) {
            wallet->setFolder(KWallet::Wallet::PasswordFolder());
            const QString entry = mResourceIdentifier + QLatin1Char(',') + QStringLiteral("$default$");
            wallet->removeEntry(entry);
        }
        delete wallet;
    }
    QFile cacheFile(mCollectionsUrlsMappingCache);
    cacheFile.remove();
}

void Settings::setResourceIdentifier(const QString &identifier)
{
    mResourceIdentifier = identifier;
}

void Settings::setDefaultPassword(const QString &password)
{
    savePassword(mResourceIdentifier, QStringLiteral("$default$"), password);
}

QString Settings::defaultPassword()
{
    return loadPassword(mResourceIdentifier, QStringLiteral("$default$"));
}

KDAV::DavUrl::List Settings::configuredDavUrls()
{
    if (mUrls.isEmpty()) {
        buildUrlsList();
    }
    KDAV::DavUrl::List davUrls;
    davUrls.reserve(mUrls.count());
    QMap<QString, UrlConfiguration *>::const_iterator it = mUrls.cbegin();
    const QMap<QString, UrlConfiguration *>::const_iterator itEnd = mUrls.cend();
    for (; it != itEnd; ++it) {
        QStringList split = it.key().split(QLatin1Char(','));
        davUrls << configuredDavUrl(KDAV::Utils::protocolByName(split.at(1)), split.at(0));
    }

    return davUrls;
}

KDAV::DavUrl Settings::configuredDavUrl(KDAV::Protocol proto, const QString &searchUrl, const QString &finalUrl)
{
    if (mUrls.isEmpty()) {
        buildUrlsList();
    }

    QUrl fullUrl;

    if (!finalUrl.isEmpty()) {
        fullUrl = QUrl::fromUserInput(finalUrl);
    } else {
        fullUrl = QUrl::fromUserInput(searchUrl);
    }

    const QString user = username(proto, searchUrl);
    fullUrl.setUserName(user);
    fullUrl.setPassword(password(proto, searchUrl));

    return KDAV::DavUrl(fullUrl, proto);
}

KDAV::DavUrl Settings::davUrlFromCollectionUrl(const QString &collectionUrl, const QString &finalUrl)
{
    if (mCollectionsUrlsMapping.isEmpty()) {
        loadMappings();
    }

    KDAV::DavUrl davUrl;
    QString targetUrl = finalUrl.isEmpty() ? collectionUrl : finalUrl;

    if (mCollectionsUrlsMapping.contains(collectionUrl)) {
        QStringList split = mCollectionsUrlsMapping[ collectionUrl ].split(QLatin1Char(','));
        if (split.size() == 2) {
            davUrl = configuredDavUrl(KDAV::Utils::protocolByName(split.at(1)), split.at(0), targetUrl);
        }
    }

    return davUrl;
}

void Settings::addCollectionUrlMapping(KDAV::Protocol proto, const QString &collectionUrl, const QString &configuredUrl)
{
    if (mCollectionsUrlsMapping.isEmpty()) {
        loadMappings();
    }

    QString value = configuredUrl + QLatin1Char(',') + KDAV::Utils::protocolName(proto);
    mCollectionsUrlsMapping.insert(collectionUrl, value);

    // Update the cache now
    //QMap<QString, QString> tmp( mCollectionsUrlsMapping );
    QFileInfo cacheFileInfo = QFileInfo(mCollectionsUrlsMappingCache);
    if (!cacheFileInfo.dir().exists()) {
        QDir::root().mkpath(cacheFileInfo.dir().absolutePath());
    }

    QFile cacheFile(mCollectionsUrlsMappingCache);
    if (cacheFile.open(QIODevice::WriteOnly)) {
        QDataStream cache(&cacheFile);
        cache.setVersion(QDataStream::Qt_4_7);
        cache << mCollectionsUrlsMapping;
        cacheFile.close();
    }
}

QStringList Settings::mappedCollections(KDAV::Protocol proto, const QString &configuredUrl)
{
    if (mCollectionsUrlsMapping.isEmpty()) {
        loadMappings();
    }

    QString value = configuredUrl + QLatin1Char(',') + KDAV::Utils::protocolName(proto);
    return mCollectionsUrlsMapping.keys(value);
}

void Settings::reloadConfig()
{
    buildUrlsList();
    updateRemoteUrls();
    loadMappings();
}

void Settings::newUrlConfiguration(Settings::UrlConfiguration *urlConfig)
{
    QString key = urlConfig->mUrl + QLatin1Char(',') + KDAV::Utils::protocolName(KDAV::Protocol(urlConfig->mProtocol));

    if (mUrls.contains(key)) {
        removeUrlConfiguration(KDAV::Protocol(urlConfig->mProtocol), urlConfig->mUrl);
    }

    mUrls[ key ] = urlConfig;
    if (urlConfig->mUser != QStringLiteral("$default$")) {
        savePassword(key, urlConfig->mUser, urlConfig->mPassword);
    }
    updateRemoteUrls();
}

void Settings::removeUrlConfiguration(KDAV::Protocol proto, const QString &url)
{
    QString key = url + QLatin1Char(',') + KDAV::Utils::protocolName(proto);

    if (!mUrls.contains(key)) {
        return;
    }

    delete mUrls[ key ];
    mUrls.remove(key);
    updateRemoteUrls();
}

Settings::UrlConfiguration *Settings::urlConfiguration(KDAV::Protocol proto, const QString &url)
{
    QString key = url + QLatin1Char(',') + KDAV::Utils::protocolName(proto);

    UrlConfiguration *ret = nullptr;
    if (mUrls.contains(key)) {
        ret = mUrls[ key ];
    }

    return ret;
}

// KDAV::Protocol Settings::protocol( const QString &url ) const
// {
//   if ( mUrls.contains( url ) )
//     return KDAV::Protocol( mUrls[ url ]->mProtocol );
//   else
//     returnKDAV::CalDav;
// }

QString Settings::username(KDAV::Protocol proto, const QString &url) const
{
    QString key = url + QLatin1Char(',') + KDAV::Utils::protocolName(proto);

    if (mUrls.contains(key)) {
        if (mUrls[ key ]->mUser == QLatin1String("$default$")) {
            return defaultUsername();
        } else {
            return mUrls[ key ]->mUser;
        }
    } else {
        return QString();
    }
}

QString Settings::password(KDAV::Protocol proto, const QString &url)
{
    QString key = url + QLatin1Char(',') + KDAV::Utils::protocolName(proto);

    if (mUrls.contains(key)) {
        if (mUrls[ key ]->mUser == QLatin1String("$default$")) {
            return defaultPassword();
        } else {
            return mUrls[ key ]->mPassword;
        }
    } else {
        return QString();
    }
}

QDateTime Settings::getSyncRangeStart() const
{
    QDateTime start = QDateTime::currentDateTimeUtc();
    start.setTime(QTime());
    const int delta = - syncRangeStartNumber().toUInt();

    if (syncRangeStartType() == QLatin1String("D")) {
        start = start.addDays(delta);
    } else if (syncRangeStartType() == QLatin1String("M")) {
        start = start.addMonths(delta);
    } else if (syncRangeStartType() == QLatin1String("Y")) {
        start = start.addYears(delta);
    } else {
        start = QDateTime();
    }

    return start;
}

void Settings::buildUrlsList()
{
    foreach (const QString &serializedUrl, remoteUrls()) {
        UrlConfiguration *urlConfig = new UrlConfiguration(serializedUrl);
        QString key = urlConfig->mUrl + QLatin1Char(',') + KDAV::Utils::protocolName(KDAV::Protocol(urlConfig->mProtocol));
        QString pass = loadPassword(key, urlConfig->mUser);
        if (!pass.isNull()) {
            urlConfig->mPassword = pass;
            mUrls[ key ] = urlConfig;
        } else {
            delete urlConfig;
        }
    }
}

void Settings::loadMappings()
{
    QString collectionsMappingCacheBase = QStringLiteral("akonadi-davgroupware/%1_c2u.dat").arg(KApplication::applicationName());
    mCollectionsUrlsMappingCache = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + collectionsMappingCacheBase;
    QFile collectionsMappingsCache(mCollectionsUrlsMappingCache);

    if (collectionsMappingsCache.exists()) {
        if (collectionsMappingsCache.open(QIODevice::ReadOnly)) {
            QDataStream cache(&collectionsMappingsCache);
            cache >> mCollectionsUrlsMapping;
            collectionsMappingsCache.close();
        }
    } else if (!collectionsUrlsMappings().isEmpty()) {
        QByteArray rawMappings = QByteArray::fromBase64(collectionsUrlsMappings().toLatin1());
        QDataStream stream(&rawMappings, QIODevice::ReadOnly);
        stream >> mCollectionsUrlsMapping;
        setCollectionsUrlsMappings(QString());
    }
}

void Settings::updateRemoteUrls()
{
    QStringList newUrls;
    newUrls.reserve(mUrls.count());

    QMapIterator<QString, UrlConfiguration *> it(mUrls);
    while (it.hasNext()) {
        it.next();
        newUrls << it.value()->serialize();
    }

    setRemoteUrls(newUrls);
}

void Settings::savePassword(const QString &key, const QString &user, const QString &password)
{
    QString entry = key + QLatin1Char(',') + user;
    mPasswordsCache[entry] = password;

    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), mWinId);
    if (!wallet) {
        return;
    }

    if (!wallet->hasFolder(KWallet::Wallet::PasswordFolder())) {
        wallet->createFolder(KWallet::Wallet::PasswordFolder());
    }

    if (!wallet->setFolder(KWallet::Wallet::PasswordFolder())) {
        return;
    }

    wallet->writePassword(entry, password);
}

QString Settings::loadPassword(const QString &key, const QString &user)
{
    QString entry;
    QString pass;

    if (user == QLatin1String("$default$")) {
        entry = mResourceIdentifier + QLatin1Char(',') + user;
    }
    else {
        entry = key + QLatin1Char(',') + user;
    }

    if (mPasswordsCache.contains(entry)) {
        return mPasswordsCache[entry];
    }

    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), mWinId);
    if (wallet) {
        if (!wallet->hasFolder(KWallet::Wallet::PasswordFolder())) {
            wallet->createFolder(KWallet::Wallet::PasswordFolder());
        }

        if (wallet->setFolder(KWallet::Wallet::PasswordFolder())) {
            if (!wallet->hasEntry(entry)) {
                pass = promptForPassword(user);
                wallet->writePassword(entry, pass);
            } else {
                wallet->readPassword(entry, pass);
            }
        }
    }

    if (pass.isNull() && !KWallet::Wallet::isEnabled()) {
        pass = promptForPassword(user);
    }

    if (!pass.isNull()) {
        mPasswordsCache[entry] = pass;
    }

    return pass;
}

QString Settings::promptForPassword(const QString &user)
{
    QPointer<QDialog> dlg = new QDialog();
    QString password;

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    dlg->setLayout(mainLayout);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(buttonBox, &QDialogButtonBox::accepted, dlg.data(), &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, dlg.data(), &QDialog::reject);

    QWidget *mainWidget = new QWidget(dlg);
    mainLayout->addWidget(mainWidget);
    mainLayout->addWidget(buttonBox);
    QVBoxLayout *vLayout = new QVBoxLayout();
    mainWidget->setLayout(vLayout);
    QLabel *label = new QLabel(i18n("A password is required for user %1",
                                    (user == QLatin1String("$default$") ? defaultUsername() : user)),
                               mainWidget
                              );
    vLayout->addWidget(label);
    QHBoxLayout *hLayout = new QHBoxLayout();
    label = new QLabel(i18n("Password: "), mainWidget);
    hLayout->addWidget(label);
    KLineEdit *lineEdit = new KLineEdit();
    lineEdit->setPasswordMode(true);
    hLayout->addWidget(lineEdit);
    vLayout->addLayout(hLayout);
    lineEdit->setFocus();

    const int result = dlg->exec();

    if (result == QDialog::Accepted && !dlg.isNull()) {
        password = lineEdit->text();
    }

    delete dlg;
    return password;
}

void Settings::updateToV2()
{
    // Take the first URL that was configured to get the username that
    // has the most chances being the default

    QStringList urls = remoteUrls();
    if (urls.isEmpty()) {
        return;
    }

    QString urlConfigStr = urls.at(0);
    UrlConfiguration urlConfig(urlConfigStr);
    QRegExp regexp(QLatin1Char('^') + urlConfig.mUser);

    QMutableStringListIterator it(urls);
    while (it.hasNext()) {
        it.next();
        it.value().replace(regexp, QStringLiteral("$default$"));
    }

    setDefaultUsername(urlConfig.mUser);
    QString key = urlConfig.mUrl + QLatin1Char(',') + KDAV::Utils::protocolName(KDAV::Protocol(urlConfig.mProtocol));
    QString pass = loadPassword(key, urlConfig.mUser);
    if (!pass.isNull()) {
        setDefaultPassword(pass);
    }
    setRemoteUrls(urls);
    setSettingsVersion(2);
    save();
}

void Settings::updateToV3()
{
    QStringList updatedUrls;

    foreach (const QString &url, remoteUrls()) {
        QStringList splitUrl = url.split(QLatin1Char('|'));

        if (splitUrl.size() == 3) {
            KDAV::Protocol protocol = Utils::protocolByTranslatedName(splitUrl.at(1));
            splitUrl[1] = KDAV::Utils::protocolName(protocol);
            updatedUrls << splitUrl.join(QLatin1Char('|'));
        }
    }

    setRemoteUrls(updatedUrls);
    setSettingsVersion(3);
    save();
}

