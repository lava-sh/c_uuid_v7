#ifndef HEXPARSE_H
#define HEXPARSE_H

#include <stddef.h>
#include <stdint.h>

int starts_with_urn_uuid(const char *text, size_t size);
int parse_uuid_hex(const char *text, size_t size, uint64_t *hi, uint64_t *lo);
int parse_uuid_hex_branchy(const char *text, size_t size, uint64_t *hi, uint64_t *lo);
int parse_uuid_hex_lut(const char *text, size_t size, uint64_t *hi, uint64_t *lo);

#endif
