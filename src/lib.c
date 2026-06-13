/*
 * Copyright (C) 2026 Lenik <observer@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "lib.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>

#include <stdio.h>

__attribute__((weak))
define_logger();

int copy_stream(FILE *in, FILE *out) {
    char buf[8192];
    size_t n;

    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            return -1;
        }
    }
    if (ferror(in)) {
        return -1;
    }
    return 0;
}

int copy_file(const char *prog, const char *path) {
    loginfo_fmt("%s: copying from %s", prog, path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: ", prog);
        perror(path);
        return -1;
    }

    int r = copy_stream(f, stdout);
    if (fclose(f) != 0) {
        r = -1;
    }
    if (r != 0) {
        fprintf(stderr, _("%s: write error\n"), prog);
        return -1;
    }
    return 0;
}
