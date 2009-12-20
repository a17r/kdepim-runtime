/*
    Copyright (c) 2009 Grégory Oestreicher <greg@kamago.net>
    
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

#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include "ui_configdialog.h"

#include <QStringList>
#include <KDialog>

class KConfigDialogManager;

namespace KWallet {
  class Wallet;
}

class ConfigDialog : public KDialog
{
  Q_OBJECT
  public:
    ConfigDialog( QWidget *parent = 0 );
    ~ConfigDialog();
    
    QStringList removedUrls() const;
    void setRemovedUrls( const QStringList &l );
    
  private slots:
    void onOkClicked();
    void onCancelClicked();
    void urlRemoved( const QString &url );
    
  private:
    Ui::ConfigDialog ui;
    KConfigDialogManager* mManager;
    QStringList rmdUrls;
};

#endif