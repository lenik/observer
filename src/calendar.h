#ifndef OBSERVER_CALENDAR_H
#define OBSERVER_CALENDAR_H

#include <wx/datetime.h>
#include <wx/string.h>

#include <ctime>
#include <string>

wxString timeOfDay(const std::string &timestamp);
int isoWeek(wxDateTime value);
int daysInMonth(wxDateTime value);
int maxDaysForMonth(wxDateTime::Month month);
int weekdayIndex(wxDateTime value, bool weekStartsMonday);
std::tm parseLocalTime(const std::string &value);
std::time_t toTimeT(const std::string &value);

wxDateTime toDateTime(const std::string &value);
wxDateTime startOfDay(wxDateTime value);
wxDateTime startOfWeek(wxDateTime value, bool weekStartsMonday);
wxDateTime startOfMonth(wxDateTime value);
wxDateTime startOfYear(wxDateTime value);

#endif