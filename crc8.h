
#ifndef CRC8_H
#define CRC8_H

#include <stddef.h>
#include <stdint.h>

typedef struct crc8_state_t crc8_state_t;
struct crc8_state_t {
    uint8_t poly;
    uint8_t crc;
};

uint8_t crc8_feed(crc8_state_t *state, const char *data, size_t nbytes);
void crc8_init(crc8_state_t *state, uint8_t poly);

#endif
