#include "random.h"

#include "platform.h"

#define UUID_RAND_MASK 0x3FFFFFFFFFFFFFFFULL
#define RESEED_MASK ((1ULL << 41) - 1ULL)
#define LOW30_MASK ((1ULL << 30) - 1ULL)
#define WYRAND_INCREMENT 0xA0761D6478BD642FULL
#define WYRAND_XOR_KEY 0xE7037ED1A0B428DBULL
#define SECURE_BUFFER_SIZE 1024U

typedef struct {
    unsigned char bytes[SECURE_BUFFER_SIZE];
    size_t offset;
    size_t available;
} secure_random_buffer_t;

static secure_random_buffer_t secure_buffer = {0};
uint64_t wyrand_state_global = 0;
int wyrand_seeded = 0;

static void secure_buffer_reset(void) {
    secure_buffer.offset = 0;
    secure_buffer.available = 0;
}

static int secure_buffer_refill(void) {
    if (fill_random(secure_buffer.bytes, sizeof(secure_buffer.bytes)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to generate random bytes");
        return -1;
    }

    secure_buffer.offset = 0;
    secure_buffer.available = sizeof(secure_buffer.bytes);
    return 0;
}

static int rnd_u64_secure(uint64_t *out) {
    if (secure_buffer.available < sizeof(*out) && secure_buffer_refill() != 0) {
        return -1;
    }

    memcpy(out, secure_buffer.bytes + secure_buffer.offset, sizeof(*out));
    secure_buffer.offset += sizeof(*out);
    secure_buffer.available -= sizeof(*out);
    return 0;
}

static uint64_t random_counter42_from_u64(const uint64_t random64) {
    return random64 & RESEED_MASK;
}

static void
random_low32_and_increment_from_u64(const uint64_t random64, uint32_t *low32, uint64_t *increment) {
    *low32 = (uint32_t)random64;
    *increment = 1U + (random64 >> 32 & 0x0FU);
}

static uint64_t next_u64(void) {
    const uint64_t value = wyrand_state_global += WYRAND_INCREMENT;

    return prng_mix64(value, value ^ WYRAND_XOR_KEY);
}

int random_ensure_seeded(void) {
    uint64_t initstate = 0;

    if (wyrand_seeded) {
        return 0;
    }

    if (fill_random((unsigned char *)&initstate, sizeof(initstate)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to seed UUIDv7 generator");
        return -1;
    }

    wyrand_state_global = initstate;
    secure_buffer_reset();

#ifdef _WIN32
    platform_seeded();
#endif
    wyrand_seeded = 1;
    return 0;
}

void random_reseed(void) {
    wyrand_seeded = 0;
    wyrand_state_global = 0;
    secure_buffer_reset();
}

uint64_t random_counter42(void) {
    return random_counter42_from_u64(next_u64());
}

int random_counter42_secure(uint64_t *counter) {
    if (rnd_u64_secure(counter) != 0) {
        return -1;
    }

    *counter = random_counter42_from_u64(*counter);
    return 0;
}

void random_split_counter42(const uint64_t counter,
                            const uint32_t low32,
                            uint16_t *rand_a,
                            uint64_t *tail62) {
    *rand_a = (uint16_t)(counter >> 30);
    *tail62 = (counter & LOW30_MASK) << 32 | (uint64_t)low32;
}

void random_next_low32_and_increment(uint32_t *low32, uint64_t *increment) {
    random_low32_and_increment_from_u64(next_u64(), low32, increment);
}

int random_next_low32_and_increment_secure(uint32_t *low32, uint64_t *increment) {
    uint64_t random64 = 0;

    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    random_low32_and_increment_from_u64(random64, low32, increment);
    return 0;
}

void random_payload(uint16_t *rand_a, uint64_t *tail62) {
    random_split_counter42(random_counter42(), (uint32_t)next_u64(), rand_a, tail62);
}

int random_payload_secure(uint16_t *rand_a, uint64_t *tail62) {
    uint64_t counter = 0;

    if (random_counter42_secure(&counter) != 0) {
        return -1;
    }
    if (rnd_u64_secure(tail62) != 0) {
        return -1;
    }

    random_split_counter42(counter, (uint32_t)*tail62, rand_a, tail62);
    return 0;
}

uint64_t random_tail62(void) {
    return next_u64() & UUID_RAND_MASK;
}

int random_tail62_secure(uint64_t *tail62) {
    if (rnd_u64_secure(tail62) != 0) {
        return -1;
    }

    *tail62 &= UUID_RAND_MASK;
    return 0;
}
