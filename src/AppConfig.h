#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string>

struct AppConfig {
    std::string theme = "dark";
    double intervalSeconds = 120.0;
    std::string storePath;
};

AppConfig& appConfig();

#endif
