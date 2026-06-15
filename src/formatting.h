#ifndef OBSERVER_FORMATTING_H
#define OBSERVER_FORMATTING_H

#include <string>

std::string ordinal(int value);
std::string trim(const std::string &value);
std::string formatDuration(long seconds);
std::string formatDouble(double value);

#endif
