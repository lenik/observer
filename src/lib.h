#ifndef LIB_H
#define LIB_H

#include <stdio.h>

int copy_stream(FILE *in, FILE *out);
int copy_file(const char *prog, const char *path);

#endif /* LIB_H */