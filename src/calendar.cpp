#include "calendar.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

wxString timeOfDay(const std::string &timestamp) {
    if (timestamp.size() >= 19) {
        return wxString::FromUTF8(timestamp.substr(11, 8).c_str());
    }
    return "";
}

int isoWeek(wxDateTime value) {
    std::tm local{};
    local.tm_year = value.GetYear() - 1900;
    local.tm_mon = static_cast<int>(value.GetMonth());
    local.tm_mday = value.GetDay();
    local.tm_isdst = -1;
    std::mktime(&local);
    char buffer[4] = {};
    std::strftime(buffer, sizeof(buffer), "%V", &local);
    return std::atoi(buffer);
}

int daysInMonth(wxDateTime value) {
    wxDateTime first = startOfMonth(value);
    wxDateTime next = first;
    next.Add(wxDateSpan::Months(1));
    next.Subtract(wxDateSpan::Days(1));
    return next.GetDay();
}

int maxDaysForMonth(wxDateTime::Month month) {
    if (month == wxDateTime::Feb) {
        return 29;
    }
    if (month == wxDateTime::Apr || month == wxDateTime::Jun || month == wxDateTime::Sep ||
        month == wxDateTime::Nov) {
        return 30;
    }
    return 31;
}

int weekdayIndex(wxDateTime value, bool weekStartsMonday) {
    if (!weekStartsMonday) {
        return value.GetWeekDay() == wxDateTime::Sun ? 0 : static_cast<int>(value.GetWeekDay());
    }
    switch (value.GetWeekDay()) {
    case wxDateTime::Mon:
        return 0;
    case wxDateTime::Tue:
        return 1;
    case wxDateTime::Wed:
        return 2;
    case wxDateTime::Thu:
        return 3;
    case wxDateTime::Fri:
        return 4;
    case wxDateTime::Sat:
        return 5;
    case wxDateTime::Sun:
        return 6;
    case wxDateTime::Inv_WeekDay:
        return 0;
    }
    return 0;
}

std::tm parseLocalTime(const std::string &value) {
    std::tm result{};
    if (value.size() >= 19) {
        std::istringstream in(value.substr(0, 19));
        in >> std::get_time(&result, "%Y-%m-%dT%H:%M:%S");
    }
    result.tm_isdst = -1;
    return result;
}

std::time_t toTimeT(const std::string &value) {
    std::tm local = parseLocalTime(value);
    return std::mktime(&local);
}

wxDateTime toDateTime(const std::string &value) {
    std::time_t time = toTimeT(value);
    if (time == static_cast<std::time_t>(-1)) {
        return wxDateTime::Now();
    }
    return wxDateTime(time);
}

wxDateTime startOfDay(wxDateTime value) {
    value.ResetTime();
    return value;
}

wxDateTime startOfWeek(wxDateTime value, bool weekStartsMonday) {
    value.ResetTime();
    const wxDateTime::WeekDay firstDay = weekStartsMonday ? wxDateTime::Mon : wxDateTime::Sun;
    while (value.GetWeekDay() != firstDay) {
        value.Subtract(wxDateSpan::Days(1));
    }
    return value;
}

wxDateTime startOfMonth(wxDateTime value) {
    return wxDateTime(1, value.GetMonth(), value.GetYear());
}

wxDateTime startOfYear(wxDateTime value) { return wxDateTime(1, wxDateTime::Jan, value.GetYear()); }
