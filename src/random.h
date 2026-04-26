#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

[[nodiscard]] int random_ensure_seeded(void);
void random_reseed(void);
[[nodiscard]] uint64_t random_counter42(void);
[[nodiscard]] int random_counter42_secure(uint64_t *counter);
void random_next_low32_and_increment(uint32_t *low32, uint64_t *increment);
[[nodiscard]] int random_next_low32_and_increment_secure(uint32_t *low32, uint64_t *increment);
void random_payload(uint16_t *rand_a, uint64_t *tail62);
[[nodiscard]] int random_payload_secure(uint16_t *rand_a, uint64_t *tail62);
[[nodiscard]] uint64_t random_tail62(void);
[[nodiscard]] int random_tail62_secure(uint64_t *tail62);
void random_split_counter42(uint64_t counter, uint32_t low32, uint16_t *rand_a, uint64_t *tail62);

#endif
