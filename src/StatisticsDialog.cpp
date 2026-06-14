#include "StatisticsDialog.h"

#include <bas/locale/i18n.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <wx/artprov.h>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/toolbar.h>

namespace {

enum {
    ID_MENU_CALENDAR = wxID_HIGHEST + 800,
    ID_MENU_DAY,
    ID_MENU_WEEK,
    ID_MENU_MONTH,
    ID_MENU_YEAR,
    ID_MENU_TODAY,
    ID_MENU_PREVIOUS,
    ID_MENU_NEXT,
};

struct ChartBucket {
    wxString label;
    int records = 0;
    long durationSeconds = 0;
    double energySum = 0.0;
    double moodSum = 0.0;
    double groundingSum = 0.0;
};

struct DaySummary {
    int records = 0;
    int nonEmptyRecords = 0;
    double energySum = 0.0;
    double moodSum = 0.0;
    double groundingSum = 0.0;
};

struct LunarDate {
    int year = 0;
    int month = 0;
    int day = 0;
    bool leap = false;
};

constexpr std::array<unsigned int, 201> LunarInfo = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5d0, 0x14573, 0x052d0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b5a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04bd7, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
    0x0d520,
};

std::tm parseLocalTime(const std::string& value)
{
    std::tm result{};
    if (value.size() >= 19) {
        std::istringstream in(value.substr(0, 19));
        in >> std::get_time(&result, "%Y-%m-%dT%H:%M:%S");
    }
    result.tm_isdst = -1;
    return result;
}

std::time_t toTimeT(const std::string& value)
{
    std::tm local = parseLocalTime(value);
    return std::mktime(&local);
}

wxDateTime toDateTime(const std::string& value)
{
    std::time_t time = toTimeT(value);
    if (time == static_cast<std::time_t>(-1)) {
        return wxDateTime::Now();
    }
    return wxDateTime(time);
}

wxDateTime startOfDay(wxDateTime value)
{
    value.ResetTime();
    return value;
}

wxDateTime startOfWeek(wxDateTime value, bool weekStartsMonday)
{
    value.ResetTime();
    const wxDateTime::WeekDay firstDay = weekStartsMonday ? wxDateTime::Mon : wxDateTime::Sun;
    while (value.GetWeekDay() != firstDay) {
        value.Subtract(wxDateSpan::Days(1));
    }
    return value;
}

wxDateTime startOfMonth(wxDateTime value)
{
    return wxDateTime(1, value.GetMonth(), value.GetYear());
}

wxDateTime startOfYear(wxDateTime value)
{
    return wxDateTime(1, wxDateTime::Jan, value.GetYear());
}

wxDateTime periodStart(wxDateTime value, StatisticsDialog::ViewMode mode, bool weekStartsMonday)
{
    switch (mode) {
    case StatisticsDialog::ViewMode::Day:
        return startOfDay(value);
    case StatisticsDialog::ViewMode::Week:
        return startOfWeek(value, weekStartsMonday);
    case StatisticsDialog::ViewMode::Month:
        return startOfMonth(value);
    case StatisticsDialog::ViewMode::Year:
        return startOfYear(value);
    case StatisticsDialog::ViewMode::Calendar:
        return startOfMonth(value);
    }
    return startOfDay(value);
}

wxDateTime periodEnd(const wxDateTime& start, StatisticsDialog::ViewMode mode)
{
    wxDateTime end = start;
    switch (mode) {
    case StatisticsDialog::ViewMode::Day:
        end.Add(wxDateSpan::Days(1));
        break;
    case StatisticsDialog::ViewMode::Week:
        end.Add(wxDateSpan::Weeks(1));
        break;
    case StatisticsDialog::ViewMode::Month:
        end.Add(wxDateSpan::Months(1));
        break;
    case StatisticsDialog::ViewMode::Year:
        end.Add(wxDateSpan::Years(1));
        break;
    case StatisticsDialog::ViewMode::Calendar:
        end.Add(wxDateSpan::Months(1));
        break;
    }
    return end;
}

long durationSeconds(const Observation& observation)
{
    const std::time_t prompted = toTimeT(observation.promptedAt);
    const std::time_t submitted = toTimeT(observation.submittedAt);
    if (prompted == static_cast<std::time_t>(-1) || submitted == static_cast<std::time_t>(-1)) {
        return 0;
    }
    return std::max(0L, static_cast<long>(std::difftime(submitted, prompted)));
}

std::string trim(const std::string& value)
{
    const std::string whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string formatDuration(long seconds)
{
    if (seconds < 0) {
        seconds = 0;
    }
    std::ostringstream out;
    if (seconds >= 3600) {
        out << seconds / 3600 << "h ";
    }
    out << (seconds % 3600) / 60 << "m " << seconds % 60 << "s";
    return out.str();
}

std::string formatDouble(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

wxString timeOfDay(const std::string& timestamp)
{
    if (timestamp.size() >= 19) {
        return wxString::FromUTF8(timestamp.substr(11, 8).c_str());
    }
    return "";
}

wxString energyEmoji(double value)
{
    if (value < 1.0) {
        return wxString::FromUTF8("⏻");
    }
    if (value < 2.5) {
        return wxString::FromUTF8("🪫");
    }
    if (value < 4.5) {
        return wxString::FromUTF8("🔋");
    }
    return wxString::FromUTF8("⚡");
}

wxString moodEmoji(double value)
{
    if (value < 1.0) {
        return wxString::FromUTF8("😢");
    }
    if (value < 2.0) {
        return wxString::FromUTF8("🙁");
    }
    if (value < 3.0) {
        return wxString::FromUTF8("😐");
    }
    if (value < 4.0) {
        return wxString::FromUTF8("🙂");
    }
    return wxString::FromUTF8("😄");
}

wxString groundingEmoji(double value)
{
    if (value < 1.0) {
        return wxString::FromUTF8("🔬");
    }
    if (value < 2.0) {
        return wxString::FromUTF8("🧪");
    }
    if (value < 3.0) {
        return wxString::FromUTF8("🪙");
    }
    if (value < 4.0) {
        return wxString::FromUTF8("💵");
    }
    return wxString::FromUTF8("💰");
}

wxString scoreWithEmoji(const wxString& emoji, double value)
{
    return emoji + " " + wxString::FromUTF8(formatDouble(value).c_str());
}

int isoWeek(wxDateTime value)
{
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

std::string ordinal(int value)
{
    const int mod100 = value % 100;
    const int mod10 = value % 10;
    const char* suffix = "th";
    if (mod100 < 11 || mod100 > 13) {
        if (mod10 == 1) {
            suffix = "st";
        } else if (mod10 == 2) {
            suffix = "nd";
        } else if (mod10 == 3) {
            suffix = "rd";
        }
    }
    std::ostringstream out;
    out << value << suffix;
    return out.str();
}

int daysInMonth(wxDateTime value)
{
    wxDateTime first = startOfMonth(value);
    wxDateTime next = first;
    next.Add(wxDateSpan::Months(1));
    next.Subtract(wxDateSpan::Days(1));
    return next.GetDay();
}

int maxDaysForMonth(wxDateTime::Month month)
{
    if (month == wxDateTime::Feb) {
        return 29;
    }
    if (month == wxDateTime::Apr || month == wxDateTime::Jun || month == wxDateTime::Sep || month == wxDateTime::Nov) {
        return 30;
    }
    return 31;
}

bool sameDay(const Observation& observation, wxDateTime day)
{
    const wxDateTime when = toDateTime(observation.promptedAt);
    return when.GetYear() == day.GetYear() && when.GetMonth() == day.GetMonth() && when.GetDay() == day.GetDay();
}

bool samePeriod(const Observation& observation, wxDateTime anchor, StatisticsDialog::ViewMode mode, bool eachYear, bool weekStartsMonday)
{
    const wxDateTime when = toDateTime(observation.promptedAt);
    if (mode == StatisticsDialog::ViewMode::Year) {
        return when.GetYear() == anchor.GetYear();
    }
    if (!eachYear) {
        switch (mode) {
        case StatisticsDialog::ViewMode::Day:
            return when.GetMonth() == anchor.GetMonth() && when.GetDay() == anchor.GetDay();
        case StatisticsDialog::ViewMode::Week:
            return isoWeek(when) == isoWeek(anchor);
        case StatisticsDialog::ViewMode::Month:
            return when.GetMonth() == anchor.GetMonth();
        case StatisticsDialog::ViewMode::Year:
        case StatisticsDialog::ViewMode::Calendar:
            return false;
        }
    }

    const wxDateTime start = periodStart(anchor, mode, weekStartsMonday);
    const wxDateTime end = periodEnd(start, mode);
    return when.IsEqualTo(start) || (when.IsLaterThan(start) && when.IsEarlierThan(end));
}

int weekdayIndex(wxDateTime value, bool weekStartsMonday)
{
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

std::vector<ChartBucket> makeBuckets(StatisticsDialog::ViewMode mode, wxDateTime anchor, bool eachYear, bool weekStartsMonday)
{
    std::vector<ChartBucket> buckets;
    switch (mode) {
    case StatisticsDialog::ViewMode::Day:
        for (int hour = 0; hour < 24; ++hour) {
            buckets.push_back({ wxString::Format("%02d", hour) });
        }
        break;
    case StatisticsDialog::ViewMode::Week:
        if (weekStartsMonday) {
            for (const char* label : { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" }) {
                buckets.push_back({ wxString::FromUTF8(label) });
            }
        } else {
            for (const char* label : { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }) {
                buckets.push_back({ wxString::FromUTF8(label) });
            }
        }
        break;
    case StatisticsDialog::ViewMode::Month:
        for (int day = 1; day <= (eachYear ? daysInMonth(anchor) : maxDaysForMonth(anchor.GetMonth())); ++day) {
            buckets.push_back({ wxString::Format("%d", day) });
        }
        break;
    case StatisticsDialog::ViewMode::Year:
        for (int month = 0; month < 12; ++month) {
            wxDateTime date(1, static_cast<wxDateTime::Month>(month), anchor.GetYear());
            buckets.push_back({ date.Format("%b") });
        }
        break;
    case StatisticsDialog::ViewMode::Calendar:
        break;
    }
    return buckets;
}

int bucketIndex(const Observation& observation, StatisticsDialog::ViewMode mode, bool weekStartsMonday)
{
    const wxDateTime when = toDateTime(observation.promptedAt);
    switch (mode) {
    case StatisticsDialog::ViewMode::Day:
        return parseLocalTime(observation.promptedAt).tm_hour;
    case StatisticsDialog::ViewMode::Week:
        return weekdayIndex(when, weekStartsMonday);
    case StatisticsDialog::ViewMode::Month:
        return when.GetDay() - 1;
    case StatisticsDialog::ViewMode::Year:
        return static_cast<int>(when.GetMonth());
    case StatisticsDialog::ViewMode::Calendar:
        return 0;
    }
    return 0;
}

wxString periodTitle(StatisticsDialog::ViewMode mode, wxDateTime anchor, bool eachYear)
{
    switch (mode) {
    case StatisticsDialog::ViewMode::Day:
        return wxString::FromUTF8(_("Day statistics")) + " "
            + (eachYear ? anchor.FormatISODate() : anchor.Format("%m-%d"));
    case StatisticsDialog::ViewMode::Week:
        return wxString::FromUTF8(_("Week statistics")) + " "
            + (eachYear ? wxString::Format("%d ", anchor.GetYear()) : "")
            + wxString::FromUTF8(ordinal(isoWeek(anchor)).c_str());
    case StatisticsDialog::ViewMode::Month:
        return wxString::FromUTF8(_("Month statistics")) + " "
            + (eachYear ? wxString::Format("%d ", anchor.GetYear()) : "")
            + anchor.Format("%b");
    case StatisticsDialog::ViewMode::Year:
        return wxString::FromUTF8(_("Year statistics")) + " " + wxString::Format("%d", anchor.GetYear());
    case StatisticsDialog::ViewMode::Calendar:
        return wxString::FromUTF8(_("Calendar")) + " " + anchor.Format("%Y %b");
    }
    return "";
}

int lunarLeapMonth(int year)
{
    if (year < 1900 || year > 2100) {
        return 0;
    }
    return LunarInfo[year - 1900] & 0xf;
}

int lunarLeapDays(int year)
{
    if (lunarLeapMonth(year) == 0) {
        return 0;
    }
    return (LunarInfo[year - 1900] & 0x10000) ? 30 : 29;
}

int lunarMonthDays(int year, int month)
{
    return (LunarInfo[year - 1900] & (0x10000 >> month)) ? 30 : 29;
}

int lunarYearDays(int year)
{
    int sum = 348;
    for (int mask = 0x8000; mask > 0x8; mask >>= 1) {
        if (LunarInfo[year - 1900] & mask) {
            ++sum;
        }
    }
    return sum + lunarLeapDays(year);
}

LunarDate solarToLunar(wxDateTime date)
{
    wxDateTime base(31, wxDateTime::Jan, 1900);
    wxDateSpan diff = date.DiffAsDateSpan(base);
    int offset = diff.GetTotalDays();
    LunarDate lunar;
    if (offset < 0) {
        return lunar;
    }

    int year = 1900;
    int daysOfYear = 0;
    while (year <= 2100 && offset >= (daysOfYear = lunarYearDays(year))) {
        offset -= daysOfYear;
        ++year;
    }
    if (year > 2100) {
        return lunar;
    }

    const int leapMonth = lunarLeapMonth(year);
    bool leap = false;
    int month = 1;
    int daysOfMonth = 0;
    while (month <= 12) {
        if (leap && month == leapMonth + 1) {
            daysOfMonth = lunarLeapDays(year);
        } else {
            daysOfMonth = lunarMonthDays(year, month);
        }
        if (offset < daysOfMonth) {
            break;
        }
        offset -= daysOfMonth;
        if (leap && month == leapMonth + 1) {
            leap = false;
        } else if (leapMonth == month) {
            leap = true;
            ++month;
        } else {
            ++month;
        }
    }

    lunar.year = year;
    lunar.month = std::min(month, 12);
    lunar.day = offset + 1;
    lunar.leap = leap;
    return lunar;
}

wxString lunarDayName(int month, int day)
{
    (void)month;
    static const char* tens[] = { "", "初", "十", "廿", "三" };
    static const char* nums[] = { "", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" };
    if (day == 10) {
        return wxString::FromUTF8("初十");
    }
    if (day == 20) {
        return wxString::FromUTF8("二十");
    }
    if (day == 30) {
        return wxString::FromUTF8("三十");
    }
    return wxString::FromUTF8(tens[day / 10]) + wxString::FromUTF8(nums[day % 10]);
}

wxString lunarMonthName(int month)
{
    static const char* names[] = { "", "正月", "二月", "三月", "四月", "五月", "六月",
        "七月", "八月", "九月", "十月", "冬月", "腊月" };
    if (month < 1 || month > 12) {
        return "";
    }
    return wxString::FromUTF8(names[month]);
}

wxString lunarLabel(wxDateTime date)
{
    const LunarDate lunar = solarToLunar(date);
    if (lunar.year == 0) {
        return "";
    }
    if (lunar.day == 1) {
        return (lunar.leap ? wxString::FromUTF8("闰") : "") + lunarMonthName(lunar.month);
    }
    return lunarDayName(lunar.month, lunar.day);
}

bool isHoliday(wxDateTime date)
{
    const int month = static_cast<int>(date.GetMonth()) + 1;
    const int day = date.GetDay();
    if ((month == 1 && day == 1)
        || (month == 2 && day == 14)
        || (month == 4 && day == 5)
        || (month == 5 && day == 1)
        || (month == 10 && day == 1)
        || (month == 12 && day == 25)) {
        return true;
    }

    const LunarDate lunar = solarToLunar(date);
    if (lunar.leap) {
        return false;
    }
    return (lunar.month == 1 && (lunar.day == 1 || lunar.day == 15))
        || (lunar.month == 5 && lunar.day == 5)
        || (lunar.month == 7 && lunar.day == 7)
        || (lunar.month == 8 && lunar.day == 15)
        || (lunar.month == 9 && lunar.day == 9)
        || (lunar.month == 12 && lunar.day == 8);
}

std::map<std::string, DaySummary> summarizeDays(const std::vector<Observation>& observations)
{
    std::map<std::string, DaySummary> summaries;
    for (const Observation& observation : observations) {
        if (observation.promptedAt.size() < 10) {
            continue;
        }
        DaySummary& summary = summaries[observation.promptedAt.substr(0, 10)];
        ++summary.records;
        if (!trim(observation.activity).empty()) {
            ++summary.nonEmptyRecords;
        }
        summary.energySum += observation.energy;
        summary.moodSum += observation.mood;
        summary.groundingSum += observation.grounding;
    }
    return summaries;
}

wxDateTime chooseMonth(wxWindow* parent, wxDateTime current, bool darkTheme)
{
    wxDialog dialog(parent, wxID_ANY, wxString::FromUTF8(_("Month")), wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetBackgroundColour(darkTheme ? wxColour(20, 23, 29) : wxColour(244, 246, 248));
    dialog.SetForegroundColour(darkTheme ? wxColour(235, 239, 245) : wxColour(28, 32, 38));

    int selectedMonth = static_cast<int>(current.GetMonth());
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* year = new wxSpinCtrl(&dialog, wxID_ANY);
    year->SetRange(1900, 2100);
    year->SetValue(current.GetYear());
    root->Add(year, 0, wxALL | wxEXPAND, 12);

    auto* grid = new wxGridSizer(3, 4, 8, 8);
    for (int month = 0; month < 12; ++month) {
        wxDateTime labelDate(1, static_cast<wxDateTime::Month>(month), current.GetYear());
        auto* button = new wxButton(&dialog, wxID_ANY, labelDate.Format("%b"));
        button->Bind(wxEVT_BUTTON, [&dialog, &selectedMonth, month](wxCommandEvent&) {
            selectedMonth = month;
            dialog.EndModal(wxID_OK);
        });
        grid->Add(button, 1, wxEXPAND);
    }
    root->Add(grid, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    dialog.SetSizerAndFit(root);
    dialog.CentreOnParent();

    if (dialog.ShowModal() == wxID_OK) {
        return wxDateTime(1, static_cast<wxDateTime::Month>(selectedMonth), year->GetValue());
    }
    return current;
}

} // namespace

class ObservationRecordTable : public wxListCtrl {
public:
    ObservationRecordTable(wxWindow* parent, bool darkTheme)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL),
          darkTheme_(darkTheme)
    {
        SetBackgroundColour(darkTheme_ ? wxColour(18, 21, 27) : wxColour(255, 255, 255));
        SetTextColour(darkTheme_ ? wxColour(232, 237, 244) : wxColour(24, 28, 34));
        Bind(wxEVT_LIST_COL_CLICK, &ObservationRecordTable::onColumnClick, this);
    }

    void setRecords(const std::vector<Observation>& records)
    {
        records_ = records;
        rebuild();
    }

private:
    enum Column {
        Prompted = 0,
        Submitted,
        Duration,
        Energy,
        Mood,
        Grounding,
        Average,
        Activity,
        Quote,
    };

    wxString columnLabel(int column) const
    {
        wxString label;
        switch (column) {
        case Prompted:
            label = wxString::FromUTF8(_("Prompted"));
            break;
        case Submitted:
            label = wxString::FromUTF8(_("Submitted"));
            break;
        case Duration:
            label = wxString::FromUTF8(_("Duration"));
            break;
        case Energy:
            label = wxString::FromUTF8(_("Energy"));
            break;
        case Mood:
            label = wxString::FromUTF8(_("Mood"));
            break;
        case Grounding:
            label = wxString::FromUTF8(_("Grounding"));
            break;
        case Average:
            label = wxString::FromUTF8(_("Avg"));
            break;
        case Activity:
            label = wxString::FromUTF8(_("Activity"));
            break;
        case Quote:
            label = wxString::FromUTF8(_("Quote"));
            break;
        default:
            label = "";
            break;
        }
        if (column == sortColumn_) {
            label += ascending_ ? wxString::FromUTF8(" ▲") : wxString::FromUTF8(" ▼");
        }
        return label;
    }

    void appendColumns()
    {
        AppendColumn(columnLabel(Prompted), wxLIST_FORMAT_LEFT, 90);
        AppendColumn(columnLabel(Submitted), wxLIST_FORMAT_LEFT, 90);
        AppendColumn(columnLabel(Duration), wxLIST_FORMAT_LEFT, 90);
        AppendColumn(columnLabel(Energy), wxLIST_FORMAT_RIGHT, 80);
        AppendColumn(columnLabel(Mood), wxLIST_FORMAT_RIGHT, 80);
        AppendColumn(columnLabel(Grounding), wxLIST_FORMAT_RIGHT, 80);
        AppendColumn(columnLabel(Average), wxLIST_FORMAT_RIGHT, 70);
        AppendColumn(columnLabel(Activity), wxLIST_FORMAT_LEFT, 280);
        AppendColumn(columnLabel(Quote), wxLIST_FORMAT_LEFT, 360);
    }

    static double avg(const Observation& observation)
    {
        return (observation.energy + observation.mood + observation.grounding) / 3.0;
    }

    int compare(const Observation& a, const Observation& b) const
    {
        switch (sortColumn_) {
        case Prompted:
            return a.promptedAt.compare(b.promptedAt);
        case Submitted:
            return a.submittedAt.compare(b.submittedAt);
        case Duration:
            return (durationSeconds(a) > durationSeconds(b)) - (durationSeconds(a) < durationSeconds(b));
        case Energy:
            return (a.energy > b.energy) - (a.energy < b.energy);
        case Mood:
            return (a.mood > b.mood) - (a.mood < b.mood);
        case Grounding:
            return (a.grounding > b.grounding) - (a.grounding < b.grounding);
        case Average:
            return (avg(a) > avg(b)) - (avg(a) < avg(b));
        case Activity:
            return trim(a.activity).compare(trim(b.activity));
        case Quote:
            return a.quote.compare(b.quote);
        default:
            return 0;
        }
    }

    void rebuild()
    {
        ClearAll();
        appendColumns();
        std::vector<Observation> rows = records_;
        std::sort(rows.begin(), rows.end(), [this](const Observation& a, const Observation& b) {
            const int result = compare(a, b);
            if (result == 0) {
                return a.promptedAt > b.promptedAt;
            }
            return ascending_ ? result < 0 : result > 0;
        });

        long index = 0;
        for (const Observation& observation : rows) {
            InsertItem(index, timeOfDay(observation.promptedAt));
            SetItem(index, 1, timeOfDay(observation.submittedAt));
            SetItem(index, 2, wxString::FromUTF8(formatDuration(durationSeconds(observation)).c_str()));
            SetItem(index, 3, scoreWithEmoji(energyEmoji(observation.energy), observation.energy));
            SetItem(index, 4, scoreWithEmoji(moodEmoji(observation.mood), observation.mood));
            SetItem(index, 5, scoreWithEmoji(groundingEmoji(observation.grounding), observation.grounding));
            SetItem(index, 6, wxString::FromUTF8(formatDouble(avg(observation)).c_str()));
            SetItem(index, 7, wxString::FromUTF8(trim(observation.activity).c_str()));
            SetItem(index, 8, wxString::FromUTF8(observation.quote.c_str()));
            if (darkTheme_) {
                SetItemTextColour(index, wxColour(232, 237, 244));
            }
            ++index;
        }
    }

    void onColumnClick(wxListEvent& event)
    {
        const int column = event.GetColumn();
        if (column == sortColumn_) {
            ascending_ = !ascending_;
        } else {
            sortColumn_ = column;
            ascending_ = column != Average;
        }
        rebuild();
    }

    std::vector<Observation> records_;
    bool darkTheme_;
    int sortColumn_ = Average;
    bool ascending_ = false;
};

class ObservationCalendarPanel : public wxPanel {
public:
    ObservationCalendarPanel(wxWindow* parent, bool darkTheme, bool weekStartsMonday)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(620, 430)),
          darkTheme_(darkTheme),
          weekStartsMonday_(weekStartsMonday)
    {
        SetMinSize(wxSize(620, 430));
        SetMaxSize(wxSize(620, 430));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ObservationCalendarPanel::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, &ObservationCalendarPanel::onLeftDown, this);
    }

    void setObservations(const std::vector<Observation>& observations)
    {
        summaries_ = summarizeDays(observations);
        Refresh();
    }

    void setMonth(wxDateTime date)
    {
        anchor_ = startOfMonth(date);
        Refresh();
    }

    void setSelected(wxDateTime date)
    {
        selected_ = startOfDay(date);
        Refresh();
    }

    void setDaySelectedHandler(std::function<void(wxDateTime)> handler)
    {
        onDaySelected_ = std::move(handler);
    }

    void setMonthChangedHandler(std::function<void(wxDateTime)> handler)
    {
        onMonthChanged_ = std::move(handler);
    }

private:
    void onPaint(wxPaintEvent&)
    {
        cellRects_.clear();
        wxAutoBufferedPaintDC dc(this);
        const wxSize size(620, 430);
        const wxColour bg = darkTheme_ ? wxColour(18, 21, 27) : wxColour(250, 251, 253);
        const wxColour fg = darkTheme_ ? wxColour(229, 235, 244) : wxColour(31, 36, 44);
        const wxColour muted = darkTheme_ ? wxColour(143, 153, 170) : wxColour(92, 99, 112);
        const wxColour selectedBg = darkTheme_ ? wxColour(42, 51, 68) : wxColour(230, 239, 255);
        const wxColour todayStroke = wxColour(255, 183, 77);
        dc.SetBackground(wxBrush(bg));
        dc.Clear();

        wxFont weekdayFont = GetFont();
        weekdayFont.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(weekdayFont);
        dc.SetTextForeground(muted);

        const int margin = 12;
        const int navH = 34;
        const int headerH = 26;
        const int cellW = std::max(1, (size.GetWidth() - margin * 2) / 7);
        const wxDateTime first = startOfMonth(anchor_);
        const int firstOffset = weekdayIndex(first, weekStartsMonday_);
        const int weeks = (firstOffset + daysInMonth(anchor_) + 6) / 7;
        const int cellH = std::max(62, (size.GetHeight() - margin * 2 - navH - headerH) / std::max(1, weeks));

        wxFont navFont = GetFont();
        navFont.SetPointSize(navFont.GetPointSize() + 2);
        navFont.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(navFont);
        dc.SetTextForeground(fg);
        prevRect_ = wxRect(margin, margin, 34, 26);
        nextRect_ = wxRect(size.GetWidth() - margin - 34, margin, 34, 26);
        const wxString monthTitle = anchor_.Format("%Y %b");
        const wxSize titleExtent = dc.GetTextExtent(monthTitle);
        titleRect_ = wxRect((size.GetWidth() - titleExtent.GetWidth()) / 2 - 16, margin,
            titleExtent.GetWidth() + 32, 26);
        dc.DrawText(wxString::FromUTF8("<"), prevRect_.GetX() + 12, prevRect_.GetY() + 2);
        dc.DrawText(wxString::FromUTF8(">"), nextRect_.GetX() + 12, nextRect_.GetY() + 2);
        dc.DrawText(monthTitle, titleRect_.GetX() + 16, titleRect_.GetY() + 2);

        const char* mondayFirst[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
        const char* sundayFirst[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        const char** weekdays = weekStartsMonday_ ? mondayFirst : sundayFirst;
        dc.SetFont(weekdayFont);
        dc.SetTextForeground(muted);
        for (int i = 0; i < 7; ++i) {
            const wxString label = wxString::FromUTF8(weekdays[i]);
            const wxSize extent = dc.GetTextExtent(label);
            dc.DrawText(label, margin + i * cellW + (cellW - extent.GetWidth()) / 2, margin + navH);
        }

        wxDateTime day = first;
        const wxDateTime today = wxDateTime::Today();

        wxFont dayFont = GetFont();
        dayFont.SetPointSize(dayFont.GetPointSize() + 2);
        dayFont.SetWeight(wxFONTWEIGHT_BOLD);
        wxFont lunarFont = GetFont();
        lunarFont.SetPointSize(std::max(7, lunarFont.GetPointSize() - 1));

        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 7; ++col) {
                const int cellIndex = row * 7 + col;
                const int x = margin + col * cellW;
                const int y = margin + navH + headerH + row * cellH;
                wxRect rect(x, y, cellW - 4, cellH - 4);
                if (cellIndex < firstOffset || day.GetMonth() != anchor_.GetMonth()) {
                    continue;
                }
                cellRects_.push_back({ rect, day });

                const bool isSelected = day.IsSameDate(selected_);
                const bool isToday = day.IsSameDate(today);
                const bool holiday = isHoliday(day);
                if (isSelected) {
                    dc.SetPen(wxPen(darkTheme_ ? wxColour(130, 137, 148) : wxColour(150, 156, 166), 1));
                    dc.SetBrush(wxBrush(selectedBg));
                    wxRect selectedRect = rect;
                    selectedRect.Deflate(7, 6);
                    dc.DrawRoundedRectangle(selectedRect, 6);
                }
                if (isToday) {
                    dc.SetPen(wxPen(todayStroke, 2));
                    dc.DrawLine(rect.GetX() + rect.GetWidth() / 3, rect.GetBottom() - 5,
                        rect.GetRight() - rect.GetWidth() / 3, rect.GetBottom() - 5);
                }

                dc.SetTextForeground(holiday ? wxColour(255, 116, 116) : fg);
                dayFont.SetPointSize(GetFont().GetPointSize() + 8);
                dc.SetFont(dayFont);
                wxString dayLabel = wxString::Format("%d", day.GetDay());
                wxSize dayExtent = dc.GetTextExtent(dayLabel);
                const int textShift = 8;
                dc.DrawText(dayLabel, rect.GetX() + (rect.GetWidth() - dayExtent.GetWidth()) / 2 - textShift, rect.GetY() + 12);

                dc.SetFont(lunarFont);
                dc.SetTextForeground(holiday ? wxColour(255, 164, 164) : muted);
                wxString lunar = lunarLabel(day);
                wxSize lunarExtent = dc.GetTextExtent(lunar);
                dc.DrawText(lunar, rect.GetX() + (rect.GetWidth() - lunarExtent.GetWidth()) / 2 - textShift, rect.GetY() + 45);

                const auto summary = summaries_.find(day.FormatISODate().ToStdString());
                if (summary != summaries_.end() && summary->second.records > 0) {
                    const DaySummary& value = summary->second;
                    const double avg = (value.energySum + value.moodSum + value.groundingSum)
                        / (3.0 * std::max(1, value.records));
                    const bool hot = value.nonEmptyRecords > 3 && avg > 3.0;
                    const int dotX = rect.GetRight() - 34;
                    const int dotY = rect.GetY() + 12;
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(hot ? wxColour(235, 86, 86) : wxColour(65, 145, 255)));
                    dc.DrawCircle(dotX, dotY, 5);
                    dc.SetTextForeground(fg);
                    dc.DrawText(wxString::Format("%d", value.nonEmptyRecords), dotX + 10, dotY - 8);
                }

                day.Add(wxDateSpan::Days(1));
            }
        }
    }

    void onLeftDown(wxMouseEvent& event)
    {
        const wxPoint point = event.GetPosition();
        if (prevRect_.Contains(point) || nextRect_.Contains(point)) {
            anchor_.Add(wxDateSpan::Months(prevRect_.Contains(point) ? -1 : 1));
            selected_ = anchor_;
            Refresh();
            if (onMonthChanged_) {
                onMonthChanged_(selected_);
            }
            return;
        }
        if (titleRect_.Contains(point)) {
            wxDateTime chosen = chooseMonth(this, anchor_, darkTheme_);
            anchor_ = startOfMonth(chosen);
            selected_ = anchor_;
            Refresh();
            if (onMonthChanged_) {
                onMonthChanged_(selected_);
            }
            return;
        }
        for (const auto& cell : cellRects_) {
            if (cell.rect.Contains(point)) {
                selected_ = startOfDay(cell.date);
                anchor_ = startOfMonth(cell.date);
                Refresh();
                if (onDaySelected_) {
                    onDaySelected_(selected_);
                }
                return;
            }
        }
    }

    struct CalendarCell {
        wxRect rect;
        wxDateTime date;
    };

    bool darkTheme_;
    bool weekStartsMonday_;
    wxDateTime anchor_ = startOfMonth(wxDateTime::Today());
    wxDateTime selected_ = wxDateTime::Today();
    std::map<std::string, DaySummary> summaries_;
    std::vector<CalendarCell> cellRects_;
    wxRect prevRect_;
    wxRect titleRect_;
    wxRect nextRect_;
    std::function<void(wxDateTime)> onDaySelected_;
    std::function<void(wxDateTime)> onMonthChanged_;
};

class StatisticsChartPanel : public wxPanel {
public:
    StatisticsChartPanel(wxWindow* parent, bool darkTheme)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 220)),
          darkTheme_(darkTheme)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &StatisticsChartPanel::onPaint, this);
    }

    void setBuckets(std::vector<ChartBucket> buckets)
    {
        buckets_ = std::move(buckets);
        Refresh();
    }

private:
    void onPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        const wxColour bg = darkTheme_ ? wxColour(18, 21, 27) : wxColour(250, 251, 253);
        const wxColour fg = darkTheme_ ? wxColour(225, 230, 238) : wxColour(40, 44, 52);
        const wxColour grid = darkTheme_ ? wxColour(44, 49, 59) : wxColour(220, 224, 230);
        dc.SetBackground(wxBrush(bg));
        dc.Clear();

        const int left = 48;
        const int right = 18;
        const int top = 28;
        const int bottom = 38;
        const int plotW = std::max(1, size.GetWidth() - left - right);
        const int plotH = std::max(1, size.GetHeight() - top - bottom);
        const int baseY = top + plotH;

        dc.SetPen(wxPen(grid));
        dc.DrawLine(left, top, left, baseY);
        dc.DrawLine(left, baseY, left + plotW, baseY);
        for (int i = 1; i < 5; ++i) {
            const int y = baseY - plotH * i / 5;
            dc.DrawLine(left, y, left + plotW, y);
        }

        if (buckets_.empty()) {
            dc.SetTextForeground(fg);
            dc.DrawText(wxString::FromUTF8(_("No records")), left + 12, top + 20);
            return;
        }

        int maxRecords = 1;
        long maxDuration = 1;
        for (const ChartBucket& bucket : buckets_) {
            maxRecords = std::max(maxRecords, bucket.records);
            maxDuration = std::max(maxDuration, bucket.durationSeconds);
        }

        const int bucketW = std::max(1, plotW / static_cast<int>(buckets_.size()));
        const int barW = std::max(2, bucketW / 5);
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            const ChartBucket& bucket = buckets_[i];
            const int x = left + static_cast<int>(i) * bucketW + bucketW / 2;
            const int countH = static_cast<int>(std::round(plotH * bucket.records / static_cast<double>(maxRecords)));
            const int durationH = static_cast<int>(std::round(plotH * bucket.durationSeconds / static_cast<double>(maxDuration)));

            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(76, 142, 255)));
            dc.DrawRectangle(x - barW - 1, baseY - countH, barW, countH);
            dc.SetBrush(wxBrush(wxColour(255, 183, 77)));
            dc.DrawRectangle(x + 1, baseY - durationH, barW, durationH);

            if (bucketW >= 28 || i % 2 == 0) {
                dc.SetTextForeground(fg);
                dc.DrawText(bucket.label, x - 10, baseY + 8);
            }
        }

        auto drawLine = [&](auto valueFor, const wxColour& color) {
            wxPoint previous;
            bool hasPrevious = false;
            dc.SetPen(wxPen(color, 2));
            dc.SetBrush(wxBrush(color));
            for (std::size_t i = 0; i < buckets_.size(); ++i) {
                const ChartBucket& bucket = buckets_[i];
                if (bucket.records <= 0) {
                    continue;
                }
                const double value = std::clamp(valueFor(bucket) / bucket.records, 0.0, 5.0);
                const int x = left + static_cast<int>(i) * bucketW + bucketW / 2;
                const int y = baseY - static_cast<int>(std::round(plotH * value / 5.0));
                if (hasPrevious) {
                    dc.DrawLine(previous.x, previous.y, x, y);
                }
                dc.DrawCircle(x, y, 3);
                previous = wxPoint(x, y);
                hasPrevious = true;
            }
        };

        drawLine([](const ChartBucket& bucket) { return bucket.energySum; }, wxColour(255, 105, 180));
        drawLine([](const ChartBucket& bucket) { return bucket.moodSum; }, wxColour(255, 235, 89));
        drawLine([](const ChartBucket& bucket) { return bucket.groundingSum; }, wxColour(92, 214, 130));

        dc.SetTextForeground(fg);
        int legendX = left;
        auto legend = [&](const wxColour& color, const wxString& text) {
            dc.SetBrush(wxBrush(color));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(legendX, 10, 12, 8);
            dc.DrawText(text, legendX + 16, 6);
            legendX += dc.GetTextExtent(text).GetWidth() + 46;
        };
        legend(wxColour(76, 142, 255), wxString::FromUTF8(_("Records")));
        legend(wxColour(255, 183, 77), wxString::FromUTF8(_("Duration")));
        legend(wxColour(255, 105, 180), "E");
        legend(wxColour(255, 235, 89), "M");
        legend(wxColour(92, 214, 130), "G");
    }

    bool darkTheme_;
    std::vector<ChartBucket> buckets_;
};

StatisticsDialog::StatisticsDialog(wxWindow* parent, std::vector<Observation> observations, std::string theme, bool weekStartsMonday)
    : wxDialog(parent, wxID_ANY, wxString::FromUTF8(_("Statistics")), wxDefaultPosition, wxSize(1040, 760),
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
      observations_(std::move(observations)),
      theme_(std::move(theme)),
      weekStartsMonday_(weekStartsMonday),
      anchor_(wxDateTime::Today())
{
    if (!observations_.empty()) {
        anchor_ = toDateTime(observations_.back().promptedAt);
    }

    const bool darkTheme = theme_ != "light";
    SetBackgroundColour(darkTheme ? wxColour(20, 23, 29) : wxColour(244, 246, 248));
    SetForegroundColour(darkTheme ? wxColour(235, 239, 245) : wxColour(28, 32, 38));

    auto* root = new wxBoxSizer(wxVERTICAL);
    wxToolBar* toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxTB_HORIZONTAL | wxTB_TEXT | wxTB_FLAT);
    toolbar->AddTool(ID_MENU_CALENDAR, wxString::FromUTF8(_("Calendar")),
        wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("F1"));
    toolbar->AddSeparator();
    toolbar->AddTool(ID_MENU_DAY, wxString::FromUTF8(_("Day")),
        wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("F5"));
    toolbar->AddTool(ID_MENU_WEEK, wxString::FromUTF8(_("Week")),
        wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("F6"));
    toolbar->AddTool(ID_MENU_MONTH, wxString::FromUTF8(_("Month")),
        wxArtProvider::GetBitmap(wxART_REPORT_VIEW, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("F7"));
    toolbar->AddTool(ID_MENU_YEAR, wxString::FromUTF8(_("Year")),
        wxArtProvider::GetBitmap(wxART_HARDDISK, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("F8"));
    toolbar->AddSeparator();
    toolbar->AddTool(ID_MENU_TODAY, wxString::FromUTF8(_("Today")),
        wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("Ctrl+T"));
    toolbar->AddTool(ID_MENU_PREVIOUS, wxString::FromUTF8(_("Previous")),
        wxArtProvider::GetBitmap(wxART_GO_BACK, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("PageUp"));
    toolbar->AddTool(ID_MENU_NEXT, wxString::FromUTF8(_("Next")),
        wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("PageDown"));
    toolbar->AddSeparator();
    toolbar->AddTool(wxID_CLOSE, wxString::FromUTF8(_("Close")),
        wxArtProvider::GetBitmap(wxART_CLOSE, wxART_TOOLBAR, wxSize(16, 16)),
        wxString::FromUTF8("Esc"));
    toolbar->Realize();

    auto* header = new wxBoxSizer(wxHORIZONTAL);
    title_ = new wxStaticText(this, wxID_ANY, "");
    wxFont titleFont = GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 5);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title_->SetFont(titleFont);

    eachYear_ = new wxCheckBox(this, wxID_ANY, wxString::FromUTF8(_("Each Year")));
    eachYear_->SetValue(true);

    header->Add(title_, 1, wxALIGN_CENTER_VERTICAL);
    header->Add(eachYear_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);

    calendar_ = new ObservationCalendarPanel(this, darkTheme, weekStartsMonday_);
    calendar_->setObservations(observations_);
    calendar_->setSelected(anchor_);
    calendar_->setDaySelectedHandler([this](wxDateTime date) {
        anchor_ = date;
        table_->setRecords(selectedDayRecords());
        updateSelectedDaySummary();
        updateTitle();
    });
    calendar_->setMonthChangedHandler([this](wxDateTime date) {
        anchor_ = date;
        table_->setRecords(selectedDayRecords());
        updateSelectedDaySummary();
        updateTitle();
    });

    daySummaryPanel_ = new wxPanel(this);
    daySummaryPanel_->SetBackgroundColour(darkTheme ? wxColour(20, 23, 29) : wxColour(244, 246, 248));
    auto* daySummarySizer = new wxBoxSizer(wxVERTICAL);
    daySummaryDate_ = new wxStaticText(daySummaryPanel_, wxID_ANY, "");
    wxFont daySummaryTitleFont = GetFont();
    daySummaryTitleFont.SetPointSize(daySummaryTitleFont.GetPointSize() + 3);
    daySummaryTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
    daySummaryDate_->SetFont(daySummaryTitleFont);
    daySummaryDate_->SetForegroundColour(darkTheme ? wxColour(235, 239, 245) : wxColour(28, 32, 38));
    auto addSummaryLine = [&](wxStaticText*& target) {
        target = new wxStaticText(daySummaryPanel_, wxID_ANY, "");
        target->SetForegroundColour(darkTheme ? wxColour(226, 232, 240) : wxColour(24, 28, 34));
        daySummarySizer->Add(target, 0, wxBOTTOM, 8);
    };
    daySummarySizer->Add(daySummaryDate_, 0, wxBOTTOM, 16);
    addSummaryLine(daySummaryRecords_);
    addSummaryLine(daySummaryEmpty_);
    addSummaryLine(daySummaryDuration_);
    addSummaryLine(daySummaryEnergy_);
    addSummaryLine(daySummaryMood_);
    addSummaryLine(daySummaryGrounding_);
    daySummaryPanel_->SetSizer(daySummarySizer);

    metricsPanel_ = new wxPanel(this);
    metricsSizer_ = new wxBoxSizer(wxHORIZONTAL);
    metricsPanel_->SetSizer(metricsSizer_);
    chart_ = new StatisticsChartPanel(this, darkTheme);
    tableTitle_ = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(_("Top activities")));
    wxFont tableTitleFont = GetFont();
    tableTitleFont.SetPointSize(tableTitleFont.GetPointSize() + 2);
    tableTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
    tableTitle_->SetFont(tableTitleFont);
    table_ = new ObservationRecordTable(this, darkTheme);

    root->Add(toolbar, 0, wxEXPAND);
    root->Add(header, 0, wxALL | wxEXPAND, 16);
    auto* calendarRow = new wxBoxSizer(wxHORIZONTAL);
    calendarRow->Add(calendar_, 0, wxRIGHT, 18);
    calendarRow->Add(daySummaryPanel_, 1, wxEXPAND);
    root->Add(calendarRow, 0, wxLEFT | wxRIGHT | wxEXPAND, 16);
    root->Add(metricsPanel_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    root->Add(chart_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    root->Add(tableTitle_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    root->Add(table_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    SetSizer(root);

    eachYear_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { render(); });
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { setMode(ViewMode::Calendar); }, ID_MENU_CALENDAR);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { setMode(ViewMode::Day); }, ID_MENU_DAY);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { setMode(ViewMode::Week); }, ID_MENU_WEEK);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { setMode(ViewMode::Month); }, ID_MENU_MONTH);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { setMode(ViewMode::Year); }, ID_MENU_YEAR);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { goToday(); }, ID_MENU_TODAY);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { movePeriod(-1); }, ID_MENU_PREVIOUS);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { movePeriod(1); }, ID_MENU_NEXT);
    Bind(wxEVT_TOOL, [this](wxCommandEvent&) { Close(); }, wxID_CLOSE);
    Bind(wxEVT_CHAR_HOOK, &StatisticsDialog::onCharHook, this);

    CentreOnParent();
    render();
}

void StatisticsDialog::onCharHook(wxKeyEvent& event)
{
    const int keyCode = event.GetKeyCode();
    if (keyCode == WXK_ESCAPE) {
        Close();
        return;
    }
    if (event.ControlDown() && (keyCode == 'T' || keyCode == 't')) {
        goToday();
        return;
    }
    if (keyCode == WXK_LEFT || keyCode == WXK_RIGHT) {
        if (mode_ == ViewMode::Calendar) {
            anchor_.Add(wxDateSpan::Days(keyCode == WXK_LEFT ? -1 : 1));
            renderCalendar();
        } else {
            const int direction = keyCode == WXK_LEFT ? -1 : 1;
            int modeIndex = static_cast<int>(mode_);
            modeIndex = (modeIndex + direction + 5) % 5;
            setMode(static_cast<ViewMode>(modeIndex));
        }
        return;
    }
    if (keyCode == WXK_UP || keyCode == WXK_DOWN) {
        if (mode_ == ViewMode::Calendar) {
            anchor_.Add(wxDateSpan::Days(keyCode == WXK_UP ? -7 : 7));
            renderCalendar();
        } else {
            movePeriod(keyCode == WXK_UP ? -1 : 1);
        }
        return;
    }
    if (keyCode == WXK_F1) {
        setMode(ViewMode::Calendar);
        return;
    }
    if (keyCode == WXK_F5) {
        setMode(ViewMode::Day);
        return;
    }
    if (keyCode == WXK_F6) {
        setMode(ViewMode::Week);
        return;
    }
    if (keyCode == WXK_F7) {
        setMode(ViewMode::Month);
        return;
    }
    if (keyCode == WXK_F8) {
        setMode(ViewMode::Year);
        return;
    }
    if (keyCode == WXK_PAGEUP) {
        movePeriod(-1);
        return;
    }
    if (keyCode == WXK_PAGEDOWN) {
        movePeriod(1);
        return;
    }
    event.Skip();
}

void StatisticsDialog::setMode(ViewMode mode)
{
    mode_ = mode;
    render();
}

void StatisticsDialog::movePeriod(int delta)
{
    switch (mode_) {
    case ViewMode::Day:
        anchor_.Add(wxDateSpan::Days(delta));
        break;
    case ViewMode::Week:
        anchor_.Add(wxDateSpan::Weeks(delta));
        break;
    case ViewMode::Month:
    case ViewMode::Calendar:
        anchor_.Add(wxDateSpan::Months(delta));
        break;
    case ViewMode::Year:
        anchor_.Add(wxDateSpan::Years(delta));
        break;
    }
    render();
}

void StatisticsDialog::render()
{
    if (mode_ == ViewMode::Calendar) {
        renderCalendar();
    } else {
        renderStatistics();
    }
}

void StatisticsDialog::updateTitle()
{
    const bool eachYear = eachYear_->GetValue();
    title_->SetLabel(periodTitle(mode_, anchor_, eachYear));
    eachYear_->Show(mode_ == ViewMode::Day || mode_ == ViewMode::Week || mode_ == ViewMode::Month);
}

void StatisticsDialog::renderCalendar()
{
    calendar_->Show();
    daySummaryPanel_->Show();
    metricsPanel_->Hide();
    chart_->Hide();
    tableTitle_->Hide();
    table_->Show();
    calendar_->setMonth(anchor_);
    calendar_->setSelected(anchor_);
    updateTitle();
    table_->setRecords(selectedDayRecords());
    updateSelectedDaySummary();
    Layout();
}

void StatisticsDialog::rebuildMetrics(const std::vector<Observation>& selected)
{
    metricsSizer_->Clear(true);

    double energy = 0.0;
    double mood = 0.0;
    double grounding = 0.0;
    long totalDuration = 0;
    int emptyCount = 0;
    for (const Observation& observation : selected) {
        energy += observation.energy;
        mood += observation.mood;
        grounding += observation.grounding;
        totalDuration += durationSeconds(observation);
        if (trim(observation.activity).empty()) {
            ++emptyCount;
        }
    }

    const double count = static_cast<double>(selected.size());
    const bool darkTheme = theme_ != "light";
    const wxColour cardBg = darkTheme ? wxColour(27, 31, 39) : wxColour(255, 255, 255);
    const wxColour fg = darkTheme ? wxColour(232, 237, 244) : wxColour(32, 36, 44);
    const wxColour muted = darkTheme ? wxColour(160, 169, 184) : wxColour(92, 99, 112);

    auto addMetric = [&](const wxArtID& artId, const wxString& label, const wxString& value) {
        auto* panel = new wxPanel(metricsPanel_);
        panel->SetBackgroundColour(cardBg);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* icon = new wxStaticBitmap(panel, wxID_ANY, wxArtProvider::GetBitmap(artId, wxART_OTHER, wxSize(24, 24)));
        auto* texts = new wxBoxSizer(wxVERTICAL);
        auto* valueText = new wxStaticText(panel, wxID_ANY, value);
        auto* labelText = new wxStaticText(panel, wxID_ANY, label);
        wxFont valueFont = GetFont();
        valueFont.SetPointSize(valueFont.GetPointSize() + 3);
        valueFont.SetWeight(wxFONTWEIGHT_BOLD);
        valueText->SetFont(valueFont);
        valueText->SetForegroundColour(fg);
        labelText->SetForegroundColour(muted);
        texts->Add(valueText, 0, wxBOTTOM, 2);
        texts->Add(labelText, 0);
        row->Add(icon, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
        row->Add(texts, 1, wxALIGN_CENTER_VERTICAL);
        panel->SetSizer(row);
        metricsSizer_->Add(panel, 1, wxRIGHT | wxEXPAND, 8);
    };

    addMetric(wxART_REPORT_VIEW, wxString::FromUTF8(_("Records")), wxString::Format("%zu", selected.size()));
    addMetric(wxART_WARNING, wxString::FromUTF8(_("Empty notes")), wxString::Format("%d", emptyCount));
    addMetric(wxART_TIP, wxString::FromUTF8(_("Total prompt duration")),
        wxString::FromUTF8(formatDuration(totalDuration).c_str()));
    addMetric(wxART_INFORMATION, wxString::FromUTF8(_("Average energy")),
        selected.empty() ? "-" : wxString::FromUTF8(formatDouble(energy / count).c_str()));
    addMetric(wxART_INFORMATION, wxString::FromUTF8(_("Average mood")),
        selected.empty() ? "-" : wxString::FromUTF8(formatDouble(mood / count).c_str()));
    addMetric(wxART_INFORMATION, wxString::FromUTF8(_("Average grounding")),
        selected.empty() ? "-" : wxString::FromUTF8(formatDouble(grounding / count).c_str()));
}

std::vector<Observation> StatisticsDialog::selectedDayRecords() const
{
    std::vector<Observation> selected;
    for (const Observation& observation : observations_) {
        if (sameDay(observation, anchor_)) {
            selected.push_back(observation);
        }
    }
    return selected;
}

void StatisticsDialog::updateSelectedDaySummary()
{
    const std::vector<Observation> selected = selectedDayRecords();
    double energy = 0.0;
    double mood = 0.0;
    double grounding = 0.0;
    long totalDuration = 0;
    int emptyCount = 0;
    for (const Observation& observation : selected) {
        energy += observation.energy;
        mood += observation.mood;
        grounding += observation.grounding;
        totalDuration += durationSeconds(observation);
        if (trim(observation.activity).empty()) {
            ++emptyCount;
        }
    }

    const double count = static_cast<double>(selected.size());
    daySummaryDate_->SetLabel(anchor_.FormatISODate());
    daySummaryRecords_->SetLabel(wxString::FromUTF8(_("Records")) + ": " + wxString::Format("%zu", selected.size()));
    daySummaryEmpty_->SetLabel(wxString::FromUTF8(_("Empty notes")) + ": " + wxString::Format("%d", emptyCount));
    daySummaryDuration_->SetLabel(wxString::FromUTF8(_("Total prompt duration")) + ": "
        + wxString::FromUTF8(formatDuration(totalDuration).c_str()));
    daySummaryEnergy_->SetLabel(wxString::FromUTF8(_("Average energy")) + ": "
        + (selected.empty() ? "-" : scoreWithEmoji(energyEmoji(energy / count), energy / count)));
    daySummaryMood_->SetLabel(wxString::FromUTF8(_("Average mood")) + ": "
        + (selected.empty() ? "-" : scoreWithEmoji(moodEmoji(mood / count), mood / count)));
    daySummaryGrounding_->SetLabel(wxString::FromUTF8(_("Average grounding")) + ": "
        + (selected.empty() ? "-" : scoreWithEmoji(groundingEmoji(grounding / count), grounding / count)));
    daySummaryPanel_->Layout();
}

void StatisticsDialog::renderStatistics()
{
    calendar_->Hide();
    daySummaryPanel_->Hide();
    metricsPanel_->Show();
    chart_->Show();
    tableTitle_->Show();
    table_->Show();
    tableTitle_->SetLabel(wxString::FromUTF8(_("Top activities")));
    updateTitle();

    const bool eachYear = eachYear_->GetValue();
    std::vector<Observation> selected;
    for (const Observation& observation : observations_) {
        if (samePeriod(observation, anchor_, mode_, eachYear, weekStartsMonday_)) {
            selected.push_back(observation);
        }
    }

    std::vector<ChartBucket> buckets = makeBuckets(mode_, anchor_, eachYear, weekStartsMonday_);
    for (const Observation& observation : selected) {
        const int index = bucketIndex(observation, mode_, weekStartsMonday_);
        if (index < 0 || index >= static_cast<int>(buckets.size())) {
            continue;
        }
        ChartBucket& bucket = buckets[index];
        ++bucket.records;
        bucket.durationSeconds += durationSeconds(observation);
        bucket.energySum += observation.energy;
        bucket.moodSum += observation.mood;
        bucket.groundingSum += observation.grounding;
    }

    rebuildMetrics(selected);
    chart_->setBuckets(std::move(buckets));
    table_->setRecords(selected);
    metricsPanel_->Layout();
    Layout();
}

void StatisticsDialog::goToday()
{
    anchor_ = wxDateTime::Today();
    render();
}
