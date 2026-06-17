#include "formatting.h"

#include <iomanip>
#include <sstream>

std::string ordinal(int value) {
    const int mod100 = value % 100;
    const int mod10 = value % 10;
    const char *suffix = "th";
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

std::string trim(std::string_view value) {
    const std::string whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    const std::size_t end = value.find_last_not_of(whitespace);
    return std::string(value.substr(start, end - start + 1));
}

std::string formatDuration(long seconds) {
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

std::string formatDouble(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string formatIntervalValue(double value) {
    if (value < 0.0) {
        value = 0.0;
    }

    std::ostringstream out;
    out << std::setprecision(15) << value;
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text.empty()) {
        text = "0";
    }
    return text;
}

double parseIntervalValue(wxString text) {
    auto std = text.ToStdString();
    auto view = std::string_view(std);
    return parseIntervalValue(view);
}

double parseIntervalValue(std::string_view text) {
    std::string copy(text);
    const char *raw = copy.c_str();

    char *end = nullptr;
    errno = 0;
    double value = std::strtod(raw, &end);
    if (errno != 0 || end == raw) {
        return 0.0;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        ++end;
    }
    if (*end != '\0') {
        return 0.0;
    }
    return value;
}
