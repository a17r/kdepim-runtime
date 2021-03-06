/*
    Copyright (c) 2010 Tobias Koenig <tokoe@kde.org>

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

#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include <KDAV/Utils>

#include <QWizard>
#include <QWizardPage>
#include <qlabel.h>

class KJob;
class KLineEdit;
class QTextBrowser;

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QRadioButton;

class SetupWizard : public QWizard
{
    Q_OBJECT

public:
    explicit SetupWizard(QWidget *parent = nullptr);

    enum {
        W_CredentialsPage,
        W_PredefinedProviderPage,
        W_ServerTypePage,
        W_ConnectionPage,
        W_CheckPage
    };

    class Url
    {
    public:
        typedef QList<Url> List;

        KDAV::Protocol protocol;
        QString url;
        QString userName;
        QString password;
    };

    Url::List urls() const;
    QString displayName() const;
};

class PredefinedProviderPage : public QWizardPage
{
public:
    explicit PredefinedProviderPage(QWidget *parent = nullptr);

    void initializePage() Q_DECL_OVERRIDE;
    int nextId() const Q_DECL_OVERRIDE;

private:
    QLabel *mLabel;
    QButtonGroup *mProviderGroup;
    QRadioButton *mUseProvider;
    QRadioButton *mDontUseProvider;
};

class CredentialsPage : public QWizardPage
{
public:
    explicit CredentialsPage(QWidget *parent = nullptr);
    int nextId() const Q_DECL_OVERRIDE;

private:
    KLineEdit *mUserName;
    KLineEdit *mPassword;
};

class ServerTypePage : public QWizardPage
{
    Q_OBJECT

public:
    explicit ServerTypePage(QWidget *parent = nullptr);

    bool validatePage() Q_DECL_OVERRIDE;

private:
    void manualConfigToggled(bool toggled);
    QButtonGroup *mServerGroup;
    QComboBox *mProvidersCombo;
};

class ConnectionPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit ConnectionPage(QWidget *parent = nullptr);

    void initializePage() Q_DECL_OVERRIDE;
    void cleanupPage() Q_DECL_OVERRIDE;

private:
    void urlElementChanged();
    QFormLayout *mLayout;
    KLineEdit *mHost;
    KLineEdit *mPath;
    QCheckBox *mUseSecureConnection;
    QFormLayout *mPreviewLayout;
    QLabel *mCalDavUrlLabel;
    QLabel *mCalDavUrlPreview;
    QLabel *mCardDavUrlLabel;
    QLabel *mCardDavUrlPreview;
    QLabel *mGroupDavUrlLabel;
    QLabel *mGroupDavUrlPreview;
};

class CheckPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit CheckPage(QWidget *parent = nullptr);

private:
    void checkConnection();
    void onFetchDone(KJob *);
    QTextBrowser *mStatusLabel;
};

#endif
