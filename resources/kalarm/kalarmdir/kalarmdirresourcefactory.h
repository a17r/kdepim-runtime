/*
  Copyright (c) 2014 Montel Laurent <montel@kde.org>

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License, version 2, as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef KALARMDIRRESOURCEFACTORY_H
#define KALARMDIRRESOURCEFACTORY_H

#include "kalarmdirresource.h"
#include <agentfactory.h>

class KalarmdirResourceFactory : public Akonadi::AgentFactory<KAlarmDirResource>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.akonadi.KalarmdirResource");
public:
    explicit KalarmdirResourceFactory(QObject *parent = Q_NULLPTR);
};

#endif
