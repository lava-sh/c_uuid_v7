#ifndef HEXPARSE_H
#define HEXPARSE_H

#include <stddef.h>
#include <stdint.h>

int parse_uuid_hex(const char *text, size_t size, uint64_t *hi, uint64_t *lo);

#endif
