#ifndef C_UUID_V7_CORE_H
#define C_UUID_V7_CORE_H

#include <stdint.h>

typedef struct {
  uint64_t hi;
  uint64_t lo;
} c_uuid_v7_uuid_words;

enum {
  C_UUID_V7_MODE_FAST = 0,
  C_UUID_V7_MODE_SECURE = 1,
};

enum {
  C_UUID_V7_OK = 0,
  C_UUID_V7_ERR_OS = 1,
  C_UUID_V7_ERR_NANOS_RANGE = 2,
  C_UUID_V7_ERR_TIMESTAMP_TOO_LARGE = 3,
};

int c_uuid_v7_build(uint64_t timestamp_s, uint8_t has_timestamp, uint64_t nanos,
                    uint8_t has_nanos, int mode, c_uuid_v7_uuid_words *out);

void c_uuid_v7_reseed(void);

#endif
