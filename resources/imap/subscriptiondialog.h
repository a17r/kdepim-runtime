/*
    Copyright (c) 2010 Klarälvdalens Datakonsult AB,
                       a KDAB Group company <info@kdab.com>
    Author: Kevin Ottens <kevin@kdab.com>

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

#ifndef SUBSCRIPTIONDIALOG_H
#define SUBSCRIPTIONDIALOG_H

#include <QDialog>

#include <krecursivefilterproxymodel.h>
#include <kimap/listjob.h>

#include <QtCore/QMap>

class QStandardItemModel;
class QStandardItem;

class QLineEdit;
class QCheckBox;
class ImapAccount;
class QTreeView;
class QPushButton;

class SubscriptionFilterProxyModel : public KRecursiveFilterProxyModel
{
    Q_OBJECT
public:
    explicit SubscriptionFilterProxyModel(QObject *parent = nullptr);

public Q_SLOTS:
    void setSearchPattern(const QString &pattern);
    void setIncludeCheckedOnly(bool checkedOnly);
    void setIncludeCheckedOnly(int checkedOnlyState);

protected:
    bool acceptRow(int sourceRow, const QModelIndex &sourceParent) const Q_DECL_OVERRIDE;

private:
    QString m_pattern;
    bool m_checkedOnly;
};

class SubscriptionDialog : public QDialog
{
    Q_OBJECT
public:
    enum Roles {
        InitialStateRole = Qt::UserRole + 1,
        PathRole
    };
    enum SubscriptionDialogOption {
        None = 0,
        AllowToEnableSubscription = 1
    };
    Q_DECLARE_FLAGS(SubscriptionDialogOptions, SubscriptionDialogOption)

    explicit SubscriptionDialog(QWidget *parent = nullptr, SubscriptionDialog::SubscriptionDialogOptions option = SubscriptionDialog::None);
    ~SubscriptionDialog();

    void connectAccount(const ImapAccount &account, const QString &password);
    bool isSubscriptionChanged() const;
    void setSubscriptionEnabled(bool enabled);
    bool subscriptionEnabled() const;

private Q_SLOTS:
    void onLoginDone(KJob *job);
    void onReloadRequested();
    void onMailBoxesReceived(const QList<KIMAP::MailBoxDescriptor> &mailBoxes,
                             const QList< QList<QByteArray> > &flags);
    void onFullListingDone(KJob *job);
    void onSubscribedMailBoxesReceived(const QList<KIMAP::MailBoxDescriptor> &mailBoxes,
                                       const QList< QList<QByteArray> > &flags);
    void onReloadDone(KJob *job);
    void onItemChanged(QStandardItem *item);

    void slotSearchPattern(const QString &pattern);

protected Q_SLOTS:
    void slotAccepted();
private:
    void readConfig();
    void writeConfig();
    void applyChanges();

    KIMAP::Session *m_session;
    bool m_subscriptionChanged;

    QTreeView *m_treeView;

    QLineEdit *m_lineEdit;
    QCheckBox *m_enableSubscription;
    SubscriptionFilterProxyModel *m_filter;
    QStandardItemModel *m_model;
    QMap<QString, QStandardItem *> m_itemsMap;
    QPushButton *mUser1Button;
};

#endif
