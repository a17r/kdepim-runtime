/*
  Copyright (c) 2010 Laurent Montel <montel@kde.org>
  Copyright (c) 2012 Christian Mollekopf <mollekopf@kolabsys.com>

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

#include "setupkolab.h"

#include "kolabproxyresource.h"
#include "setupdefaultfoldersjob.h"
#include "upgradejob.h"

#include <AkonadiCore/AgentManager>

#include <KMessageBox>
#include <KGlobal>

#include <QProcess>
#include <QStandardPaths>
#include <KConfigGroup>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#define IMAP_RESOURCE_IDENTIFIER QLatin1String("akonadi_imap_resource")

SetupKolab::SetupKolab(KolabProxyResource *parentResource)
    :  QDialog(),
       m_ui(new Ui::SetupKolabView),
       m_versionUi(new Ui::ChangeFormatView),
       m_parentResource(parentResource)
{
    QWidget *mainWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    setLayout(mainLayout);
    m_ui->setupUi(mainWidget);
    mainLayout->addWidget(mainWidget);
    m_ui->setupUi(mainWidget);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SetupKolab::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SetupKolab::reject);
    mainLayout->addWidget(buttonBox);
    initConnection();
    updateCombobox();
}

SetupKolab::~SetupKolab()
{
    delete m_ui;
    delete m_versionUi;
}

KConfigGroup SetupKolab::getConfigGroup()
{
    //This is a bit of a hack, but we have to reload the config file in case it was edited by the setupwizard (it's not reloaded automatically).
    //Without this the config will not be able to load the correct values until restarted (e.g. akonadiconsole).
    KSharedConfigPtr config = KGlobal::mainComponent().config();
    config->reparseConfiguration();
    return KConfigGroup(KGlobal::mainComponent().config(), "KolabProxyResourceSettings");
}

Kolab::Version SetupKolab::readKolabVersion(const QString &resourceIdentifier)
{
    KConfigGroup grp(getConfigGroup());
    if (resourceIdentifier.isEmpty()) {
        return Kolab::KolabV3;
    }
    const QString key(QLatin1String("KolabFormatVersion") + resourceIdentifier);
    Kolab::Version version = static_cast<Kolab::Version>(
                                 grp.readEntry<int>(key, static_cast<int>(Kolab::KolabV3)));
    return version;
}

void SetupKolab::initConnection()
{
    connect(m_ui->launchWizard, &QPushButton::clicked, this, &SetupKolab::slotLaunchWizard);
    connect(m_ui->createKolabFolderButton, &QPushButton::clicked, this, &SetupKolab::slotCreateDefaultKolabCollections);
    connect(m_ui->upgradeFormatButton, &QPushButton::clicked, this, &SetupKolab::slotShowUpgradeDialog);
    connect(m_ui->imapAccountComboBox, static_cast<void (KComboBox::*)(const QString &)>(&KComboBox::currentIndexChanged), this, &SetupKolab::slotSelectedAccountChanged);
    connect(Akonadi::AgentManager::self(), &Akonadi::AgentManager::instanceAdded, this, &SetupKolab::slotInstanceAddedRemoved);
    connect(Akonadi::AgentManager::self(), &Akonadi::AgentManager::instanceRemoved, this, &SetupKolab::slotInstanceAddedRemoved);
}

void SetupKolab::slotShowUpgradeDialog()
{
    const Akonadi::AgentInstance instanceSelected =
        m_agentList[m_ui->imapAccountComboBox->currentText()];

    QDialog *dialog = new QDialog(this);
    QWidget *mainWidget = new QWidget(dialog);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    dialog->setLayout(mainLayout);
    mainLayout->addWidget(mainWidget);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    dialog->connect(buttonBox, &QDialogButtonBox::accepted, this, &SetupKolab::accept);
    dialog->connect(buttonBox, &QDialogButtonBox::rejected, this, &SetupKolab::reject);
    mainLayout->addWidget(buttonBox);
    m_versionUi->setupUi(mainWidget);
    m_versionUi->progressBar->setDisabled(true);
    connect(m_versionUi->pushButton, &QPushButton::clicked, this, &SetupKolab::slotDoUpgrade);

    Kolab::Version v = readKolabVersion(instanceSelected.identifier());

    m_versionUi->formatVersion->insertItem(0, i18n("Kolab Format v2"), Kolab::KolabV2);
    m_versionUi->formatVersion->insertItem(1, i18n("Kolab Format v3"), Kolab::KolabV3);
    if (v == Kolab::KolabV2) {
        m_versionUi->formatVersion->setCurrentIndex(0);
    } else {
        m_versionUi->formatVersion->setCurrentIndex(1);
    }
    KConfigGroup grp(getConfigGroup());
    m_versionUi->upgradeGroupBox->setEnabled(grp.readEntry<bool>("UpgradeEnabled", false));
    dialog->exec();
    grp.writeEntry(
        QLatin1String("KolabFormatVersion") + instanceSelected.identifier(),
        m_versionUi->formatVersion->itemData(m_versionUi->formatVersion->currentIndex()));
    grp.sync();
    slotSelectedAccountChanged();
    dialog->deleteLater();
}

void SetupKolab::slotDoUpgrade()
{
    const Akonadi::AgentInstance instanceSelected =
        m_agentList[m_ui->imapAccountComboBox->currentText()];

    m_versionUi->statusLabel->setText(i18n("Started"));
    m_versionUi->progressBar->setEnabled(true);

    UpgradeJob *job =
        new UpgradeJob(
        static_cast<Kolab::Version>(
            m_versionUi->formatVersion->itemData(
                m_versionUi->formatVersion->currentIndex()).toInt()),
        instanceSelected, this);
    connect(job, SIGNAL(percent(KJob*,ulong)),
            this, SLOT(slotUpgradeProgress(KJob*,ulong)));
    connect(job, &UpgradeJob::result, this, &SetupKolab::slotUpgradeDone);
}

void SetupKolab::slotUpgradeProgress(KJob *job, ulong value)
{
    Q_UNUSED(job);
    m_versionUi->progressBar->setValue(value);
}

void SetupKolab::slotUpgradeDone(KJob *job)
{
    if (job->error()) {
        qWarning() << job->errorString();
        m_versionUi->statusLabel->setText(i18n("Error"));
        KMessageBox::error(
            this,
            i18n("Could not complete the upgrade process: ") + job->errorString(),
            i18n("Error during Upgrade Process"));
        return;
    }
    m_versionUi->statusLabel->setText(i18n("Complete"));
    m_versionUi->progressBar->setValue(100);
}

void SetupKolab::slotSelectedAccountChanged()
{
    const Akonadi::AgentInstance instanceSelected =
        m_agentList[m_ui->imapAccountComboBox->currentText()];

    Kolab::Version v = readKolabVersion(instanceSelected.identifier());

    if (v == Kolab::KolabV2) {
        m_ui->formatVersion->setText(i18n("Kolab Format v2"));
    } else {
        m_ui->formatVersion->setText(i18n("Kolab Format v3"));
    }
}

void SetupKolab::updateCombobox()
{
    bool imapAccountFound = false;
    m_ui->imapAccountComboBox->clear();
    m_agentList.clear();

    foreach (const Akonadi::AgentInstance &instance, Akonadi::AgentManager::self()->instances()) {
        if (instance.identifier().contains(IMAP_RESOURCE_IDENTIFIER)) {
            const QString instanceName = instance.name();
            m_agentList.insert(instanceName, instance);
            m_ui->imapAccountComboBox->addItem(instanceName);
            imapAccountFound = true;
        }
    }
    if (imapAccountFound) {
        m_ui->stackedWidget->setCurrentIndex(1);
    } else {
        m_ui->stackedWidget->setCurrentIndex(0);
    }
}

void SetupKolab::slotLaunchWizard()
{
    QStringList lst;
    lst.append(QLatin1String("--assistant"));
    lst.append(QLatin1String("imap"));

    const QString path = QStandardPaths::findExecutable(QLatin1String("accountwizard"));
    if (!QProcess::startDetached(path, lst)) {
        KMessageBox::error(
            this,
            i18n("Could not start the account wizard. "
                 "Please check your installation."),
            i18n("Unable to start account wizard"));
    }
}

void SetupKolab::slotInstanceAddedRemoved()
{
    updateCombobox();
}

void SetupKolab::slotCreateDefaultKolabCollections()
{
    const Akonadi::AgentInstance instanceSelected =
        m_agentList[m_ui->imapAccountComboBox->currentText()];

    if (instanceSelected.isValid()) {
        new SetupDefaultFoldersJob(instanceSelected, this);
    }
}

