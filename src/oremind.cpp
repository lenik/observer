/*
 * Copyright (C) 2026 Lenik <observer@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "oremind.h"

#include "config.h"
#include "lib.h"
#include "AppConfig.h"
#include "ObserverApp.h"

#include <wx/wx.h>

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <sys/stat.h>

#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

define_logger();
wxIMPLEMENT_APP_NO_MAIN(ObserverApp);

enum { OPT_VERSION = 256 };

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
    fputs("  -t, --theme THEME  ", out);
    fputs(_("set theme: light or dark\n"), out);
    fputs("  -i, --interval NUM ", out);
    fputs(_("set prompt interval in minutes\n"), out);
    fputs("  -d, --sqlite-db PATH ", out);
    fputs(_("SQLite file path, or log directory when PATH is a directory\n"), out);
    fputs("      --version      ", out);
    fputs(_("output version information and exit\n"), out);
    fputs("\n", out);
    fprintf(out, _("Report bugs to: <%s>\n"), PROJECT_EMAIL);
}

int main(int argc, char **argv) {
    const char *exe = argc > 0 ? argv[0] : "oremind";
    init_i18n(LOCALEDIR);

    static const struct option long_opts[] = {
        {"verbose", no_argument, NULL, 'v'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {"theme", required_argument, NULL, 't'},
        {"interval", required_argument, NULL, 'i'},
        {"sqlite-db", required_argument, NULL, 'd'},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "vqht:i:d:", long_opts, NULL);
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
        case 't':
            if (strcmp(optarg, "light") != 0 && strcmp(optarg, "dark") != 0) {
                fprintf(stderr, _("invalid theme: %s\n"), optarg);
                return 1;
            }
            appConfig().theme = optarg;
            break;
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
        case 'd':
            appConfig().storePath = optarg;
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

    return wxEntry(argc, argv);
}
