/*
 * Copyright (C) 2026 Lenik <observer@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "oremind.h"

#include "config.h"
#include "AppConfig.h"
#include "LayoutDiag.h"
#include "UiTheme.h"
#include "ObserverApp.h"

#include <wx/wx.h>

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <filesystem>
#include <sys/stat.h>

#include <getopt.h>
#include <limits.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

define_logger();
wxIMPLEMENT_APP_NO_MAIN(ObserverApp);

enum { OPT_VERSION = 256, OPT_DIAG = 257 };

void set_locale_env(const char* locale) {
    if (locale == nullptr || *locale == '\0') {
        return;
    }

    std::string lang = locale;
    if (lang == "ja") {
        lang = "ja_JP.utf8";
    } else if (lang == "ko") {
        lang = "ko_KR.utf8";
    } else if (lang == "zh_CN") {
        lang = "zh_CN.utf8";
    } else if (lang == "zh_TW") {
        lang = "zh_TW.utf8";
    } else if (lang.find('.') == std::string::npos && lang != "C" && lang != "POSIX") {
        lang += ".utf8";
    }
    setenv("LANGUAGE", locale, 1);
    setenv("LC_ALL", lang.c_str(), 1);
    setenv("LANG", lang.c_str(), 1);
    appConfig().locale = locale;
}

void bind_dev_locale_dir(const char* argv0) {
    namespace fs = std::filesystem;
    const char* domain = "observer";
    std::error_code ec;

    fs::path exe_path;
#if defined(__linux__)
    char resolved[PATH_MAX] = {};
    ssize_t length = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
    if (length > 0) {
        resolved[length] = '\0';
        exe_path = resolved;
    }
#endif
    if (exe_path.empty() && argv0 != nullptr) {
        exe_path = fs::weakly_canonical(argv0, ec);
    }
    if (exe_path.empty()) {
        return;
    }

    fs::path candidate = exe_path.parent_path() / "po";
    if (fs::is_directory(candidate, ec)) {
        bindtextdomain(domain, candidate.c_str());
    }
}

const char* preparse_locale(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--locale") == 0) {
            if (i + 1 < argc) {
                return argv[i + 1];
            }
            return nullptr;
        }
        const char* prefix = "--locale=";
        const size_t prefix_len = strlen(prefix);
        if (strncmp(argv[i], prefix, prefix_len) == 0) {
            return argv[i] + prefix_len;
        }
    }
    return nullptr;
}

void usage(FILE *out) {
    fputs(_("Usage: oremind [OPTION]...\n"
            "Run a desktop self-observation prompt in the background.\n"),
          out);
    fputs("\n", out);
    fputs("  -v, --verbose      ", out);
    fputs(_("repeat for more verbose loggings\n"), out);
    fputs("  -q, --quiet        ", out);
    fputs(_("show less logging messages\n"), out);
    fputs("  -h, --help         ", out);
    fputs(_("display this help and exit\n"), out);
    fputs("  -l, --locale LOCALE ", out);
    fputs(_("set UI locale, for example zh_CN, ja, ko, en\n"), out);
    fputs("  -t, --theme THEME  ", out);
    fputs(_("set theme: dark, light, innocent, maiden, girl, morandi, github, ios, msdos, windows\n"), out);
    fputs("  -o, --opacity NUM  ", out);
    fputs(_("set final dialog opacity percentage, 0 to 100\n"), out);
    fputs("  -c, --cancel NUM   ", out);
    fputs(_("exit after NUM consecutive cancels\n"), out);
    fputs("  -i, --interval NUM ", out);
    fputs(_("set prompt interval in minutes\n"), out);
    fputs("  -w, --weekstart MmSs ", out);
    fputs(_("set calendar week start: M/m for Monday, S/s for Sunday\n"), out);
    fputs("  -d, --database PATH ", out);
    fputs(_("SQLite file path, or log directory when PATH is a directory\n"), out);
    fputs("      --diag         ", out);
    fputs(_("dump dialog layout to layout.toml and exit\n"), out);
    fputs("      --version      ", out);
    fputs(_("output version information and exit\n"), out);
    fputs("\n", out);
    fprintf(out, _("Report bugs to: <%s>\n"), PROJECT_EMAIL);
}

int main(int argc, char **argv) {
    const char *exe = argc > 0 ? argv[0] : "oremind";
    const char* preparsed_locale = preparse_locale(argc, argv);
    if (preparsed_locale != nullptr && *preparsed_locale != '\0') {
        set_locale_env(preparsed_locale);
    }
    setlocale(LC_ALL, "");
    init_i18n(LOCALEDIR);
    bind_dev_locale_dir(exe);
    bind_textdomain_codeset("observer", "UTF-8");
    textdomain("observer");

    static const struct option long_opts[] = {
        {"verbose", no_argument, NULL, 'v'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {"locale", required_argument, NULL, 'l'},
        {"theme", required_argument, NULL, 't'},
        {"opacity", required_argument, NULL, 'o'},
        {"cancel", required_argument, NULL, 'c'},
        {"interval", required_argument, NULL, 'i'},
        {"weekstart", required_argument, NULL, 'w'},
        {"database", required_argument, NULL, 'd'},
        {"diag", no_argument, NULL, OPT_DIAG},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "vqhl:t:o:c:i:w:d:", long_opts, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'v':
            log_more();
            break;
        case 'q':
            log_less();
            break;
        case 'h':
            usage(stdout);
            return 0;
        case 'l':
            if (optarg == nullptr || *optarg == '\0') {
                fprintf(stderr, _("invalid locale: %s\n"), optarg != nullptr ? optarg : "");
                return 1;
            }
            set_locale_env(optarg);
            break;
        case 't':
            {
                if (optarg == nullptr || *optarg == '\0') {
                    fprintf(stderr, _("expect theme name.\n"));
                    return 1;
                }
                const std::string normalized = normalizeUiTheme(optarg);
                if (normalized.empty()) {
                    fprintf(stderr, _("invalid theme: %s\n"), optarg);
                    return 1;
                }
                appConfig().theme = normalized;
            }
            break;
        case 'o': {
            char* end = nullptr;
            errno = 0;
            long opacity = strtol(optarg, &end, 10);
            if (errno != 0 || end == optarg || *end != '\0' || opacity < 0 || opacity > 100) {
                fprintf(stderr, _("invalid opacity: %s\n"), optarg);
                return 1;
            }
            appConfig().opacityPercent = static_cast<int>(opacity);
            break;
        }
        case 'c': {
            char* end = nullptr;
            errno = 0;
            long cancelCount = strtol(optarg, &end, 10);
            if (errno != 0 || end == optarg || *end != '\0' || cancelCount < 1 || cancelCount > INT_MAX) {
                fprintf(stderr, _("invalid cancel count: %s\n"), optarg);
                return 1;
            }
            appConfig().cancelExitCount = static_cast<int>(cancelCount);
            break;
        }
        case 'i': {
            char* end = nullptr;
            errno = 0;
            double intervalMinutes = strtod(optarg, &end);
            if (errno != 0 || end == optarg || *end != '\0' || intervalMinutes < 0.0) {
                fprintf(stderr, _("invalid interval: %s\n"), optarg);
                return 1;
            }
            appConfig().intervalSeconds = intervalMinutes * 60.0;
            break;
        }
        case 'w':
            if (strcmp(optarg, "M") == 0 || strcmp(optarg, "m") == 0) {
                appConfig().weekStartsMonday = true;
            } else if (strcmp(optarg, "S") == 0 || strcmp(optarg, "s") == 0) {
                appConfig().weekStartsMonday = false;
            } else {
                fprintf(stderr, _("invalid week start: %s\n"), optarg);
                return 1;
            }
            break;
        case 'd':
            appConfig().storePath = optarg;
            break;
        case OPT_DIAG:
            appConfig().diagMode = true;
            break;
        case OPT_VERSION:
            printf("oremind %s\n", PROJECT_VERSION);
            printf(_("Copyright (C) %d %s\n"), PROJECT_YEAR, PROJECT_AUTHOR);
            fputs(_("License AGPL-3.0-or-later: <https://www.gnu.org/licenses/agpl-3.0.html>\n"),
                  stdout);
            fputs(_("This is free software: you are free to change and redistribute it.\n"),
                  stdout);
            fputs(_("This project opposes AI exploitation and AI hegemony.\n"), stdout);
            fputs(_("This project rejects mindless MIT-style licensing and politically naive "
                    "BSD-style licensing.\n"),
                  stdout);
            fputs(_("There is NO WARRANTY, to the extent permitted by law.\n"), stdout);
            return 0;
        default:
            usage(stderr);
            return 1;
        }
    }

    argc -= optind;
    argv += optind;

    loginfo_fmt("%s: verbose mode enabled", exe);

    int wxArgc = 1;
    char *wxArgv[] = {const_cast<char *>(exe), nullptr};
    return wxEntry(wxArgc, wxArgv);
}
