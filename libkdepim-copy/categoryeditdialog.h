/*
    This file is part of libkdepim.

    Copyright (c) 2000, 2001, 2002 Cornelius Schumacher <schumacher@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
    Boston, MA 02111-1307, USA.
*/
#ifndef KPIM_CATEGORYEDITDIALOG_H
#define KPIM_CATEGORYEDITDIALOG_H

#include <libkdepim/categoryeditdialog_base.h>

class KPimPrefs;

namespace KPIM {

class CategoryEditDialog : public CategoryEditDialog_base
{ 
    Q_OBJECT
  public:
    CategoryEditDialog( KPimPrefs *prefs, QWidget* parent = 0,
                        const char* name = 0, 
                        bool modal = FALSE, WFlags fl = 0 );
    ~CategoryEditDialog();

  public slots:
    void add();
    void remove();
    void modify();

    void slotOk();
    void slotApply();
    
  signals:
    void categoryConfigChanged();

  private slots:
    void editItem(QListViewItem *item);
    void slotTextChanged(const QString &text);

  private:
    KPimPrefs *mPrefs;
};

}

#endif
