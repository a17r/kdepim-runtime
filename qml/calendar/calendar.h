#ifndef CALENDAR_H
#define CALENDAR_H

#include <QObject>
#include <QDate>
#include <QAbstractListModel>
#include <QAbstractItemModel>
#include <kcalendarsystem.h>

#include "calendardata.h"
#include "daydata.h"
#include "daysmodel.h"
#include "calendardayhelper.h"

class Calendar : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QDate startDate READ startDate WRITE setStartDate NOTIFY startDateChanged)
    Q_PROPERTY(int types READ types WRITE setTypes NOTIFY typesChanged)
    Q_PROPERTY(int sorting READ sorting WRITE setSorting NOTIFY sortingChanged)
    Q_PROPERTY(int days READ days WRITE setDays NOTIFY daysChanged)
    Q_PROPERTY(int weeks READ weeks WRITE setWeeks NOTIFY weeksChanged)
    Q_PROPERTY(int startDay READ startDay WRITE setStartDay NOTIFY startDayChanged)
    Q_PROPERTY(int year READ year CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString monthName READ monthName CONSTANT)
    Q_PROPERTY(QAbstractListModel* model READ model CONSTANT)
    Q_PROPERTY(QList<int> weeksModel READ weeksModel NOTIFY startDateChanged CONSTANT)
    Q_PROPERTY(QAbstractItemModel* selectedDayModel READ selectedDayModel CONSTANT)

    Q_ENUMS(Type)
    Q_ENUMS(Sort)

public:
    enum Type {
        Holiday = 1,
        Event = 2,
        Todo = 4,
        Journal = 8
    };
    Q_DECLARE_FLAGS(Types, Type)

    enum Sort {
        Ascending = 1,
        Descending = 2,
        None = 4
    };
    Q_DECLARE_FLAGS(Sorting, Sort)


    explicit Calendar(QObject *parent = 0);

    // Start date
    QDate startDate() const;
    void setStartDate(const QDate &dateTime);

    // Types
    int types() const;
    void setTypes(int types);

    // Sorting
    int sorting() const;
    void setSorting(int sorting);

    // Days
    int days();
    void setDays(int days);

    // Weeks
    int weeks();
    void setWeeks(int weeks);

    // Start day
    int startDay();
    void setStartDay(int day);

    // Error message
    QString errorMessage() const;

    // Month name
    QString monthName() const;
    int year() const;
 
    // Model containing all events for the given date range
    QAbstractListModel* model() const;

    // Model filter that only gived the events for a selected day
    QAbstractItemModel* selectedDayModel() const;

    QList<int> weeksModel() const;


    // QML invokables
    Q_INVOKABLE void nextMonth();
    Q_INVOKABLE void nextYear();
    Q_INVOKABLE void previousMonth();
    Q_INVOKABLE void previousYear();
    Q_INVOKABLE QString dayName(int weekday) const ;
    Q_INVOKABLE void setSelectedDay(int year, int month, int day) const;
    
signals:
    void startDateChanged();
    void typesChanged();
    void daysChanged();
    void weeksChanged();
    void startDayChanged();
    void errorMessageChanged();
    void sortingChanged();

public slots:
    void updateData();
    
private:
    QDate m_startDate;
    Types m_types;
    Sorting m_sorting;
    QList<DayData> m_dayList;
    QList<int> m_weekList;
    DaysModel* m_model;
    CalendarDayHelper* m_dayHelper;
    int m_days;
    int m_weeks;
    int m_startDay;
    QString m_errorMessage;
    CalendarData* m_calDataPerDay;

    
};

#endif // CALENDAR_H
