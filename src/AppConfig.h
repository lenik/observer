#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string>

struct AppConfig {
    std::string theme = "dark";
    std::string locale;
    int opacityPercent = 75;
    int cancelExitCount = 5;
    double intervalSeconds = 300.0;
    std::string storePath;
    bool weekStartsMonday = true;
    bool diagMode = false;
};

AppConfig& appConfig();

#endif
