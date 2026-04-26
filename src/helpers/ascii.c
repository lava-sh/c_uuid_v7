#include "ascii.h"

unsigned char ascii_lower(const unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (unsigned char)(ch | 0x20) : ch;
}
