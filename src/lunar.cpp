#include "lunar.h"

#include <array>
#include <algorithm>

constexpr std::array<unsigned int, 201> LunarInfo = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2, //
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977, //
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, //
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950, //
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, //
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5d0, 0x14573, 0x052d0, 0x0a9a8, 0x0e950, 0x06aa0, //
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, //
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b5a0, 0x195a6, //
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570, //
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0, //
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, //
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, //
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, //
    0x05aa0, 0x076a3, 0x096d0, 0x04bd7, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, //
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, //
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0, //
    0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4, //
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0, //
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160, //
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252, //
    0x0d520,
};

int lunarLeapMonth(int year) {
    if (year < 1900 || year > 2100) {
        return 0;
    }
    return LunarInfo[year - 1900] & 0xf;
}

int lunarLeapDays(int year) {
    if (lunarLeapMonth(year) == 0) {
        return 0;
    }
    return (LunarInfo[year - 1900] & 0x10000) ? 30 : 29;
}

int lunarMonthDays(int year, int month) {
    return (LunarInfo[year - 1900] & (0x10000 >> month)) ? 30 : 29;
}

int lunarYearDays(int year) {
    int sum = 348;
    for (int mask = 0x8000; mask > 0x8; mask >>= 1) {
        if (LunarInfo[year - 1900] & mask) {
            ++sum;
        }
    }
    return sum + lunarLeapDays(year);
}

LunarDate solarToLunar(wxDateTime date) {
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

wxString lunarDayName(int month, int day) {
    (void)month;
    static const char *tens[] = {"", "初", "十", "廿", "三"};
    static const char *nums[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
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

wxString lunarMonthName(int month) {
    static const char *names[] = {"",     "正月", "二月", "三月", "四月", "五月", "六月",
                                  "七月", "八月", "九月", "十月", "冬月", "腊月"};
    if (month < 1 || month > 12) {
        return "";
    }
    return wxString::FromUTF8(names[month]);
}

wxString lunarLabel(wxDateTime date) {
    const LunarDate lunar = solarToLunar(date);
    if (lunar.year == 0) {
        return "";
    }
    if (lunar.day == 1) {
        return (lunar.leap ? wxString::FromUTF8("闰") : "") + lunarMonthName(lunar.month);
    }
    return lunarDayName(lunar.month, lunar.day);
}

bool isHoliday(wxDateTime date) {
    const int month = static_cast<int>(date.GetMonth()) + 1;
    const int day = date.GetDay();
    if ((month == 1 && day == 1) || (month == 2 && day == 14) || (month == 4 && day == 5) ||
        (month == 5 && day == 1) || (month == 10 && day == 1) || (month == 12 && day == 25)) {
        return true;
    }

    const LunarDate lunar = solarToLunar(date);
    if (lunar.leap) {
        return false;
    }
    return (lunar.month == 1 && (lunar.day == 1 || lunar.day == 15)) ||
           (lunar.month == 5 && lunar.day == 5) || (lunar.month == 7 && lunar.day == 7) ||
           (lunar.month == 8 && lunar.day == 15) || (lunar.month == 9 && lunar.day == 9) ||
           (lunar.month == 12 && lunar.day == 8);
}
