/*
    Copyright 2009 Constantin Berzan <exit3219@gmail.com>

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

#include "synctest.h"

#include <QDBusInterface>
#include <QTime>

#include <QDebug>

#include <AkonadiCore/AgentInstance>
#include <AkonadiCore/AgentManager>
#include <AkonadiCore/Control>
#include <qtest_akonadi.h>
#include <QSignalSpy>

#define TIMES 100 // How many times to sync.
#define TIMEOUT 10 // How many seconds to wait before declaring the resource dead.

using namespace Akonadi;

void SyncTest::initTestCase()
{
    QVERIFY(Control::start());
    QTest::qWait(1000);
}

void SyncTest::testSync()
{
    AgentInstance instance = AgentManager::self()->instance(QStringLiteral("akonadi_maildir_resource_0"));
    QVERIFY(instance.isValid());

    for (int i = 0; i < TIMES; ++i) {
        QDBusInterface *interface = new QDBusInterface(
            QStringLiteral("org.freedesktop.Akonadi.Resource.%1").arg(instance.identifier()),
            QStringLiteral("/"), QStringLiteral("org.freedesktop.Akonadi.Resource"), QDBusConnection::sessionBus(), this);
        QVERIFY(interface->isValid());
        QTime t;
        t.start();
        instance.synchronize();
        QSignalSpy spy(interface, SIGNAL(synchronized()));
        QVERIFY(spy.wait(TIMEOUT * 1000));
        qDebug() << "Sync attempt" << i << "in" << t.elapsed() << "ms.";
        delete interface;
    }
}

QTEST_AKONADIMAIN(SyncTest)

