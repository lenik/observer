#ifndef OBSERVER_LUNAR_H
#define OBSERVER_LUNAR_H

#include <wx/datetime.h>

struct LunarDate {
    int year = 0;
    int month = 0;
    int day = 0;
    bool leap = false;
};

int lunarLeapMonth(int year);
int lunarLeapDays(int year);
int lunarMonthDays(int year, int month);
int lunarYearDays(int year);
LunarDate solarToLunar(wxDateTime date);
wxString lunarDayName(int month, int day);
wxString lunarMonthName(int month);
wxString lunarLabel(wxDateTime date);
bool isHoliday(wxDateTime date);

#endif