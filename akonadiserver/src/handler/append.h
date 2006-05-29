/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.             *
 ***************************************************************************/
#ifndef AKONADIAPPEND_H
#define AKONADIAPPEND_H

#include <handler.h>

#include <QDateTime>

namespace Akonadi {

/**
  Handler for the Append command.
 */
class Append : public Handler
{
public:
    Append();

    ~Append();

    bool handleLine(const QByteArray& line);
    bool handleContinuation( const QByteArray& line );

protected:
    bool commit();
    bool inContinuation() const;
    bool allDataRead() const;
    bool startContinuation();

private:
    int m_size;
    QByteArray m_data;
    QByteArray m_mailbox;
    QDateTime m_dateTime;
    QList<QByteArray> m_flags;

};

}

#endif
