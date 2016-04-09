
#include "crc8.h"

static uint8_t crc8_feed_byte(crc8_state_t *state, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        if (((state->crc >> 7) & 1) != ((byte >> i) & 1)) {
            state->crc = (state->crc << 1) ^ state->poly;
        } else {
            state->crc = state->crc << 1;
        }
    }
    return state->crc;
}

uint8_t crc8_feed(crc8_state_t *state, const char *data, size_t nbytes)
{
    for (int i = 0; i < nbytes; i++) {
        crc8_feed_byte(state, data[i]);
    }
    return state->crc;
}

void crc8_init(crc8_state_t *state, uint8_t poly)
{
    state->crc = 0;
    state->poly = poly;
}
