#include "ascii.h"

unsigned char ascii_lower(const unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (unsigned char)(ch | 0x20);
    }
    return ch;
}
