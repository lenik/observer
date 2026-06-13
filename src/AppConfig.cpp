#include "AppConfig.h"

AppConfig& appConfig()
{
    static AppConfig config;
    return config;
}
