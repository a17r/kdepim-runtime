/* This file is part of the KDE project
   Copyright (C) 2009 Kevin Ottens <ervin@kde.org>
   Copyright (C) 2006-2008 Omat Holding B.V. <info@omat.nl>
   Copyright (C) 2006 Frode M. Døving <frode@lnix.net>

   Original copied from showfoto:
    Copyright 2005 by Gilles Caulier <caulier.gilles@free.fr>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "setupserver.h"
#include "settings.h"
#include "imapresource.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QRadioButton>

#include <mailtransport/transport.h>
#include <mailtransport/servertest.h>

#include <akonadi/collectionfetchjob.h>
#include <akonadi/kmime/specialmailcollections.h>
#include <akonadi/kmime/specialmailcollectionsrequestjob.h>

#include <kemailsettings.h>
#include <klocale.h>
#include <kpushbutton.h>
#include <kmessagebox.h>
#include <kuser.h>
#include <solid/networking.h>

#include <kpimidentities/identitymanager.h>
#include <kpimidentities/identitycombo.h>

#include "imapaccount.h"
#include "subscriptiondialog.h"

#ifdef KDEPIM_MOBILE_UI
#include "ui_setupserverview_mobile.h"
#else
#include "ui_setupserverview_desktop.h"
#endif

SetupServer::SetupServer( ImapResource *parentResource, WId parent )
  : KDialog(), m_parentResource( parentResource ), m_ui(new Ui::SetupServerView), m_serverTest(0),
    m_subscriptionsChanged(false), m_shouldClearCache(false)
{
#ifdef KDEPIM_MOBILE_UI
  setButtonsOrientation( Qt::Vertical );
#endif
  Settings::self()->setWinId( parent );
  m_ui->setupUi( mainWidget() );
  m_ui->safeImapGroup->setId( m_ui->noRadio, 1 );
  m_ui->safeImapGroup->setId( m_ui->sslRadio, 2 );
  m_ui->safeImapGroup->setId( m_ui->tlsRadio, 3 );

  connect( m_ui->noRadio, SIGNAL( toggled(bool) ),
           this, SLOT( slotSafetyChanged() ) );
  connect( m_ui->sslRadio, SIGNAL( toggled(bool) ),
           this, SLOT( slotSafetyChanged() ) );
  connect( m_ui->tlsRadio, SIGNAL( toggled(bool) ),
           this, SLOT( slotSafetyChanged() ) );

  populateDefaultAuthenticationOptions();

  m_ui->testInfo->hide();
  m_ui->testProgress->hide();
  m_ui->accountName->setFocus();
  m_ui->checkInterval->setSuffix( ki18np( " minute", " minutes" ) );

  // FIXME: This option has no effect yet, therefore hide it for now.
  m_ui->includeInCheck->hide();

  m_ui->folderRequester->setMimeTypeFilter(
    QStringList() << QLatin1String( "message/rfc822" ) );
  m_ui->folderRequester->setAccessRightsFilter( Akonadi::Collection::CanChangeItem | Akonadi::Collection::CanCreateItem | Akonadi::Collection::CanDeleteItem );
  m_identityManager = new KPIMIdentities::IdentityManager( false, this, "mIdentityManager" );
  m_identityCombobox = new KPIMIdentities::IdentityCombo( m_identityManager, this );
  m_ui->identityLabel->setBuddy( m_identityCombobox );
  m_ui->identityLayout->addWidget( m_identityCombobox, 1 );
  m_ui->identityLabel->setBuddy( m_identityCombobox );


  connect( m_ui->testButton, SIGNAL( pressed() ), SLOT( slotTest() ) );

  connect( m_ui->imapServer, SIGNAL( textChanged( const QString & ) ),
           SLOT( slotTestChanged() ) );
  connect( m_ui->imapServer, SIGNAL( textChanged( const QString & ) ),
           SLOT( slotComplete() ) );
  connect( m_ui->userName, SIGNAL( textChanged( const QString & ) ),
           SLOT( slotComplete() ) );
  connect( m_ui->subscriptionEnabled, SIGNAL( toggled(bool) ), this, SLOT( slotSubcriptionCheckboxChanged() ) );
  connect( m_ui->subscriptionButton, SIGNAL( pressed() ), SLOT( slotManageSubscriptions() ) );

  connect( m_ui->managesieveCheck, SIGNAL(toggled(bool)),
           SLOT(slotEnableWidgets()) );
  connect( m_ui->sameConfigCheck, SIGNAL(toggled(bool)),
           SLOT(slotEnableWidgets()) );

  connect( m_ui->useDefaultIdentityCheck, SIGNAL( toggled(bool) ), this, SLOT( slotIdentityCheckboxChanged() ) );
  connect( m_ui->enableMailCheckBox, SIGNAL( toggled(bool) ), this, SLOT( slotMailCheckboxChanged() ) );
  connect( m_ui->safeImapGroup, SIGNAL( buttonClicked( int ) ), this, SLOT( slotEncryptionRadioChanged() ) );

  readSettings();
  slotTestChanged();
  slotComplete();
  connect( Solid::Networking::notifier(),
           SIGNAL( statusChanged( Solid::Networking::Status ) ),
           SLOT( slotTestChanged() ) );
  connect( this, SIGNAL( applyClicked() ),
           SLOT( applySettings() ) );
  connect( this, SIGNAL( okClicked() ),
           SLOT( applySettings() ) );
}

SetupServer::~SetupServer()
{
}

bool SetupServer::shouldClearCache() const
{
  return m_shouldClearCache;
}

void SetupServer::slotSubcriptionCheckboxChanged()
{
  m_ui->subscriptionButton->setEnabled( m_ui->subscriptionEnabled->isChecked() );
}

void SetupServer::slotIdentityCheckboxChanged()
{
  m_identityCombobox->setEnabled( !m_ui->useDefaultIdentityCheck->isChecked() );
}

void SetupServer::slotMailCheckboxChanged()
{
  m_ui->checkInterval->setEnabled( m_ui->enableMailCheckBox->isChecked() );
}

void SetupServer::slotEncryptionRadioChanged()
{
  // TODO these really should be defined somewhere else
  switch ( m_ui->safeImapGroup->checkedId() ) {
  case 1:
  case 3:
    m_ui->portSpin->setValue( 143 );
    break;
  case 2:
    m_ui->portSpin->setValue( 993 );
    break;
  default:
    kFatal() << "Shouldn't happen";
  }

}

#include <Akonadi/CollectionModifyJob>

void SetupServer::applySettings()
{
  m_shouldClearCache = ( Settings::self()->imapServer() != m_ui->imapServer->text() )
                    || ( Settings::self()->userName() != m_ui->userName->text() );

  m_parentResource->setName( m_ui->accountName->text() );

  Settings::self()->setImapServer( m_ui->imapServer->text() );
  Settings::self()->setUserName( m_ui->userName->text() );
  Settings::self()->setSafety( m_ui->safeImapGroup->checkedId() );
  Settings::self()->setAuthentication( m_ui->authenticationCombo->itemData( m_ui->authenticationCombo->currentIndex() ).toInt() );
  Settings::self()->setPassword( m_ui->password->text() );
  Settings::self()->setSubscriptionEnabled( m_ui->subscriptionEnabled->isChecked() );
  Settings::self()->setIntervalCheckTime( m_ui->checkInterval->value() );
  Settings::self()->setDisconnectedModeEnabled( m_ui->disconnectedModeEnabled->isChecked() );

  Settings::self()->setSieveSupport( m_ui->managesieveCheck->isChecked() );
  Settings::self()->setSieveReuseConfig( m_ui->sameConfigCheck->isChecked() );
  Settings::self()->setSievePort( m_ui->sievePortSpin->value() );
  Settings::self()->setSieveAlternateUrl( m_ui->alternateURL->text() );
  Settings::self()->setSieveVacationFilename( m_vacationFileName );

  Settings::self()->setTrashCollection( m_ui->folderRequester->collection().id() );

  Settings::self()->setAutomaticExpungeEnabled( m_ui->autoExpungeCheck->isChecked() );
  Settings::self()->setUseDefaultIdentity( m_ui->useDefaultIdentityCheck->isChecked() );

  if ( !m_ui->useDefaultIdentityCheck->isChecked() )
    Settings::self()->setAccountIdentity( m_identityCombobox->currentIdentity() );

  Settings::self()->setIntervalCheckEnabled( m_ui->enableMailCheckBox->isChecked() );
  if( m_ui->enableMailCheckBox->isChecked() )
    Settings::self()->setIntervalCheckTime( m_ui->checkInterval->value() );

  Settings::self()->setIncludeInManualCheck( m_ui->includeInCheck->isChecked() );

  Settings::self()->writeConfig();
  kDebug() << "wrote" << m_ui->imapServer->text() << m_ui->userName->text() << m_ui->safeImapGroup->checkedId();

  if ( m_oldResourceName != m_ui->accountName->text() && !m_ui->accountName->text().isEmpty() ) {
    m_parentResource->renameRootCollection( m_ui->accountName->text() );
  }
}

void SetupServer::readSettings()
{
  if ( m_parentResource->name() == m_parentResource->identifier() )
    m_parentResource->setName( i18n( "IMAP Account" ) );
  m_ui->accountName->setText( m_parentResource->name() );
  m_oldResourceName = m_ui->accountName->text();

  KUser* currentUser = new KUser();
  KEMailSettings esetting;

  m_ui->imapServer->setText(
    !Settings::self()->imapServer().isEmpty() ? Settings::self()->imapServer() :
    esetting.getSetting( KEMailSettings::InServer ) );

  m_ui->userName->setText(
    !Settings::self()->userName().isEmpty() ? Settings::self()->userName() :
    currentUser->loginName() );

  int i = Settings::self()->safety();
  if ( i == 0 )
    i = 1; // it crashes when 0, shouldn't happen, but you never know.
  m_ui->safeImapGroup->button( i )->setChecked( true );

  i = Settings::self()->authentication();
  m_ui->authenticationCombo->setCurrentIndex( m_ui->authenticationCombo->findData( i ) );

  if ( !Settings::self()->passwordPossible() ) {
    m_ui->password->setEnabled( false );
    KMessageBox::information( 0, i18n( "Could not access KWallet. "
                                       "If you want to store the password permanently then you have to "
                                       "activate it. If you do not "
                                       "want to use KWallet, check the box below, but note that you will be "
                                       "prompted for your password when needed." ),
                              i18n( "Do not use KWallet" ), "warning_kwallet_disabled" );
  } else {
    m_ui->password->insert( Settings::self()->password() );
  }

  m_ui->subscriptionEnabled->setChecked( Settings::self()->subscriptionEnabled() );

  m_ui->checkInterval->setValue( Settings::self()->intervalCheckTime() );
  m_ui->disconnectedModeEnabled->setChecked( Settings::self()->disconnectedModeEnabled() );

  m_ui->managesieveCheck->setChecked(Settings::self()->sieveSupport());
  m_ui->sameConfigCheck->setChecked( Settings::self()->sieveReuseConfig() );
  m_ui->sievePortSpin->setValue( Settings::self()->sievePort() );
  m_ui->alternateURL->setText( Settings::self()->sieveAlternateUrl() );
  m_vacationFileName = Settings::self()->sieveVacationFilename();


  Akonadi::Collection trashCollection( Settings::self()->trashCollection() );
  if ( trashCollection.isValid() ) {
    Akonadi::CollectionFetchJob *fetchJob = new Akonadi::CollectionFetchJob( trashCollection,Akonadi::CollectionFetchJob::Base,this );
    connect( fetchJob, SIGNAL(collectionsReceived(Akonadi::Collection::List)),
             this, SLOT(targetCollectionReceived(Akonadi::Collection::List)) );
  }
  else {
    Akonadi::SpecialMailCollectionsRequestJob *requestJob = new Akonadi::SpecialMailCollectionsRequestJob( this );
    connect ( requestJob, SIGNAL(result(KJob*)),
              this, SLOT(localFolderRequestJobFinished(KJob*)) );
    requestJob->requestDefaultCollection( Akonadi::SpecialMailCollections::Trash );
    requestJob->start();
  }

  m_ui->useDefaultIdentityCheck->setChecked( Settings::self()->useDefaultIdentity() );
  if ( !m_ui->useDefaultIdentityCheck->isChecked() )
    m_identityCombobox->setCurrentIdentity( Settings::self()->accountIdentity() );

  m_ui->enableMailCheckBox->setChecked( Settings::self()->intervalCheckEnabled() );
  if( m_ui->enableMailCheckBox->isChecked() )
    m_ui->checkInterval->setValue( Settings::self()->intervalCheckTime() );

  m_ui->includeInCheck->setChecked( Settings::self()->includeInManualCheck() );

  m_ui->autoExpungeCheck->setChecked( Settings::self()->automaticExpungeEnabled() );

  if ( m_vacationFileName.isEmpty() )
    m_vacationFileName = "kmail-vacation.siv";

  delete currentUser;
}

void SetupServer::slotTest()
{
  kDebug() << m_ui->imapServer->text();

  m_ui->testButton->setEnabled( false );
  m_ui->safeImap->setEnabled( false );
  m_ui->authenticationCombo->setEnabled( false );

  m_ui->testInfo->clear();
  m_ui->testInfo->hide();

  delete m_serverTest;
  m_serverTest = new MailTransport::ServerTest( this );

  QString server = m_ui->imapServer->text().section( ':', 0, 0 );
  int port = m_ui->imapServer->text().section( ':', 1, 1 ).toInt();

  m_serverTest->setServer( server );

  if ( port>0 ) {
    m_serverTest->setPort( MailTransport::Transport::EnumEncryption::None, port );
    m_serverTest->setPort( MailTransport::Transport::EnumEncryption::SSL, port );
  }

  m_serverTest->setProtocol( "imap" );
  m_serverTest->setProgressBar( m_ui->testProgress );
  connect( m_serverTest, SIGNAL( finished( QList<int> ) ),
           SLOT( slotFinished( QList<int> ) ) );
  m_serverTest->start();
}

void SetupServer::slotFinished( QList<int> testResult )
{
  kDebug() << testResult;

  using namespace MailTransport;

  m_ui->testInfo->show();

  m_ui->sslRadio->setEnabled( testResult.contains( Transport::EnumEncryption::SSL ) );
  m_ui->tlsRadio->setEnabled( testResult.contains( Transport::EnumEncryption::TLS ) );
  m_ui->noRadio->setEnabled( testResult.contains( Transport::EnumEncryption::None ) );

  QString text;
  if ( testResult.contains( Transport::EnumEncryption::TLS ) ) {
    m_ui->tlsRadio->setChecked( true );
    text = i18n( "<qt><b>TLS is supported and recommended.</b></qt>" );
  } else if ( testResult.contains( Transport::EnumEncryption::SSL ) ) {
    m_ui->sslRadio->setChecked( true );
    text = i18n( "<qt><b>SSL is supported and recommended.</b></qt>" );
  } else if ( testResult.contains( Transport::EnumEncryption::None ) ) {
    m_ui->noRadio->setChecked( true );
    text = i18n( "<qt><b>No security is supported. It is not "
                 "recommended to connect to this server.</b></qt>" );
  } else {
    text = i18n( "<qt><b>It is not possible to use this server.</b></qt>" );
  }
  m_ui->testInfo->setText( text );

  m_ui->testButton->setEnabled( true );
  m_ui->safeImap->setEnabled( true );
  m_ui->authenticationCombo->setEnabled( true );
}

void SetupServer::slotTestChanged()
{
  delete m_serverTest;
  m_serverTest = 0;
  slotSafetyChanged();

  // do not use imapConnectionPossible, as the data is not yet saved.
  m_ui->testButton->setEnabled( true /* TODO Global::connectionPossible() ||
                                        m_ui->imapServer->text() == "localhost"*/ );
}

void SetupServer::slotEnableWidgets()
{
  bool haveSieve = m_ui->managesieveCheck->isChecked();
  bool reuseConfig = m_ui->sameConfigCheck->isChecked();

  m_ui->sameConfigCheck->setEnabled( haveSieve );
  m_ui->sievePortSpin->setEnabled( haveSieve && reuseConfig );
  m_ui->alternateURL->setEnabled( haveSieve && !reuseConfig );
}

void SetupServer::slotComplete()
{
  bool ok =  !m_ui->imapServer->text().isEmpty() && !m_ui->userName->text().isEmpty();
  button( KDialog::Ok )->setEnabled( ok );
}

void SetupServer::slotSafetyChanged()
{
  if ( m_serverTest==0 ) {
    m_ui->noRadio->setEnabled( true );
    m_ui->sslRadio->setEnabled( true );
    m_ui->tlsRadio->setEnabled( true );

    populateDefaultAuthenticationOptions();
    m_ui->authenticationCombo->setEnabled( true );
    return;
  }

  QList<int> protocols;

  switch ( m_ui->safeImapGroup->checkedId() ) {
  case 1:
    protocols = m_serverTest->normalProtocols();
    break;
  case 2:
    protocols = m_serverTest->secureProtocols();
    break;
  case 3:
    protocols = m_serverTest->tlsProtocols();
    break;
  default:
    kFatal() << "Shouldn't happen";
  }

  m_ui->authenticationCombo->clear();
  foreach( int prot, protocols ) {
    KIMAP::LoginJob::AuthenticationMode t = ( KIMAP::LoginJob::AuthenticationMode ) prot;
    m_ui->authenticationCombo->addItem( KIMAP::LoginJob::authenticationModeString( t ) , prot );
  }
}

void SetupServer::slotManageSubscriptions()
{
  ImapAccount account;
  account.setServer( m_ui->imapServer->text() );
  account.setUserName( m_ui->userName->text() );
  account.setName( m_ui->imapServer->text() + '/' + m_ui->userName->text() );
  account.setSubscriptionEnabled( m_ui->subscriptionEnabled->isChecked() );

  switch ( m_ui->safeImapGroup->checkedId() ) {
  case 1:
    account.setEncryptionMode( KIMAP::LoginJob::Unencrypted );
    break;
  case 2:
    account.setEncryptionMode( KIMAP::LoginJob::AnySslVersion );
    break;
  case 3:
    account.setEncryptionMode( KIMAP::LoginJob::TlsV1 );
    break;
  default:
    kFatal("Shouldn't happen...");
  }

  int type = m_ui->authenticationCombo->itemData( m_ui->authenticationCombo->currentIndex() ).toInt();
  account.setAuthenticationMode( ( KIMAP::LoginJob::AuthenticationMode ) type );

  m_subscriptionsChanged = false;
  SubscriptionDialog *subscriptions = new SubscriptionDialog( this, i18n("Serverside Subscription..."), &account, m_subscriptionsChanged );

  connect( &account, SIGNAL( success( KIMAP::Session* ) ),
           subscriptions, SLOT( slotConnectionSuccess() ) );

  account.connect( m_ui->password->text() );
  subscriptions->exec();

  m_ui->subscriptionEnabled->setChecked( account.isSubscriptionEnabled() );
}


void SetupServer::targetCollectionReceived( Akonadi::Collection::List collections )
{
  m_ui->folderRequester->setCollection( collections.first() );
}

void SetupServer::localFolderRequestJobFinished( KJob *job )
{
  if ( !job->error() ) {
    Akonadi::Collection targetCollection = Akonadi::SpecialMailCollections::self()->defaultCollection( Akonadi::SpecialMailCollections::Trash );
    Q_ASSERT( targetCollection.isValid() );
    m_ui->folderRequester->setCollection( targetCollection );
  }
}

#define addAuthenticationItem( type ) \
    m_ui->authenticationCombo->addItem( KIMAP::LoginJob::authenticationModeString( type ), QVariant( type ) );

void SetupServer::populateDefaultAuthenticationOptions()
{
  m_ui->authenticationCombo->clear();
  addAuthenticationItem( KIMAP::LoginJob::ClearText );
  addAuthenticationItem( KIMAP::LoginJob::Login );
  addAuthenticationItem( KIMAP::LoginJob::Plain );
  addAuthenticationItem( KIMAP::LoginJob::CramMD5 );
  addAuthenticationItem( KIMAP::LoginJob::DigestMD5 );
  addAuthenticationItem( KIMAP::LoginJob::NTLM );
  addAuthenticationItem( KIMAP::LoginJob::GSSAPI );
  addAuthenticationItem( KIMAP::LoginJob::Anonymous );
}




#include "setupserver.moc"
