#ifndef WORDS_H
#define WORDS_H

#include <stdint.h>

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo);
void bytes_to_words_loop(const unsigned char *bytes, uint64_t *hi, uint64_t *lo);

#endif
