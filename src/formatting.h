#ifndef OBSERVER_FORMATTING_H
#define OBSERVER_FORMATTING_H

#include <wx/string.h>

#include <string>
#include <string_view>

std::string ordinal(int value);
std::string trim(std::string_view value);

std::string formatDuration(long seconds);
std::string formatDouble(double value);

std::string formatIntervalValue(double value);
double parseIntervalValue(std::string_view text);

#endif
