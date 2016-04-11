#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "crc8.h"


static const char *port = "/dev/ttyUSB0";

#define UART_1W_ADDR_LEN (8)
#define UART_1W_SCRATCHPAD_LEN (9)

typedef uint8_t onewire_addr_t[UART_1W_ADDR_LEN];
typedef uint8_t onewire_scratchpad_t[UART_1W_SCRATCHPAD_LEN];

enum ONEWIRE_STATUS {
    ONEWIRE_PRESENCE = 0,
    ONEWIRE_NCONN = 1,
    ONEWIRE_ERROR = 2,
    ONEWIRE_EMPTY = 3,
    ONEWIRE_CRC_ERROR = 4,
};


void set_baudrate(int fd, speed_t speed)
{
    struct termios port_settings;
    memset(&port_settings, 0, sizeof(port_settings));
    cfsetispeed(&port_settings, speed);
    cfsetospeed(&port_settings, speed);
    cfmakeraw(&port_settings);
    tcsetattr(fd, TCSANOW, &port_settings);
    tcflush(fd, TCIOFLUSH);
    usleep(1000);
}


uint8_t onewire_control_probe(int fd, uint8_t signal)
{
    set_baudrate(fd, B9600);
    write(fd, &signal, 1);
    uint8_t response = 0x00;
    read(fd, &response, 1);
    //printf("control probe sig %02x resp %02x\n", signal, response);
    set_baudrate(fd, B115200);
    sleep(1);
    return response;
}


uint8_t onewire_probe(int fd, uint8_t signal)
{
    tcflush(fd, TCIOFLUSH);
    write(fd, &signal, 1);
    uint8_t response;
    read(fd, &response, 1);
    //fprintf(stderr, "probe sig %02x resp %02x\n", signal, response);
    return response;
}


int onewire_reset(int fd)
{
    uint8_t response = onewire_control_probe(fd, 0xf0);
    if ((response & 0xF0) < 0xF0) {
        return ONEWIRE_PRESENCE;
    }

    return ONEWIRE_EMPTY;
}


void onewire_write1(int fd)
{
    onewire_probe(fd, 0xFF);
}


void onewire_write0(int fd)
{
    onewire_probe(fd, 0x00);
}


uint8_t onewire_read(int fd)
{
    // startbit pulls the bus low, then keep the bus high
    return onewire_probe(fd, 0xFF) == 0xFF;
}

void onewire_write_byte(int fd, uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        usleep(10);
        if (byte & 0x1) {
            onewire_write1(fd);
        } else {
            onewire_write0(fd);
        }
        byte >>= 1;
    }
}

uint8_t onewire_read_byte(int fd)
{
    uint8_t result = 0x00;
    for (int8_t shift = 0; shift < 8; shift++)
    {
        if (onewire_read(fd)) {
            result |= (1 << shift);
        }
    }
    return result;
}


int onewire_findnext(int fd, onewire_addr_t addr)
{
    uint8_t status = onewire_reset(fd);
    if (status != ONEWIRE_PRESENCE) {
        return status | 0x40;
    }

    // this algorithm requires two scans over the whole address range to find
    // the next ROM address (which is then already selected)
    uint8_t previous_alternative_bit = 0xff;
    // initiate search
    onewire_write_byte(fd, 0xF0);

    for (uint_least8_t offs = 0; offs < UART_1W_ADDR_LEN*8; offs++)
    {
        const uint8_t false_presence = !onewire_read(fd);
        const uint8_t true_presence = !onewire_read(fd);
        (void)false_presence;


        const uint8_t byteaddr = (offs & 0xF8) >> 3;
        const uint8_t bitoffs = (offs & 0x07);

        uint8_t prevbit = addr[byteaddr] & (1<<bitoffs);

        /* lcd_writestate(false_presence, true_presence, prevbit); */

        if (!prevbit) {
            // note this as a possible position to return to on the second
            // iteration, as a higher value is possible here
            if (true_presence) {
                previous_alternative_bit = offs;
            }
            if (!false_presence) {
                // we can abort here; the device which possibly was at the
                // current address is not here anymore.
                break;
            }
        } else if (!true_presence) {
            // dito
            break;
        }

        if (prevbit) {
            onewire_write1(fd);
        } else {
            onewire_write0(fd);
        }
    }

    // we found no higher possible address
    if (previous_alternative_bit == 0xff) {
        return ONEWIRE_EMPTY;
    }

    // re-initialize the bus for searching the next device
    status = onewire_reset(fd);
    if (status != ONEWIRE_PRESENCE) {
        return status | 0x80;
    }

    onewire_write_byte(fd, 0xF0);

    for (uint_least8_t offs = 0; offs < previous_alternative_bit; offs++)
    {
        // ignore the presence strobes, although we *could* use them as a safety
        // net. if the device isn’t there anymore, we’ll find out later.
        onewire_read(fd);
        onewire_read(fd);
        const uint8_t byteaddr = (offs & 0xF8) >> 3;
        const uint8_t bitoffs = (offs & 0x07);

        if (addr[byteaddr] & (1<<bitoffs)) {
            onewire_write1(fd);
        } else {
            onewire_write0(fd);
        }
    }

    // okay, we have the equal part of the address, let’s continue with the new
    // part.

    // not looking for 'false' devices
    onewire_read(fd);

    if (onewire_read(fd)) {
        // the device we’re expecting is not here ...
        return ONEWIRE_ERROR;
    }

    // we’re definitely going down the 'true' route; we only have to set that
    // bit now ...
    onewire_write1(fd);

    {
        const uint8_t byteaddr = (previous_alternative_bit & 0xF8) >> 3;
        const uint8_t bitoffs = (previous_alternative_bit & 0x07);
        addr[byteaddr] |= (1<<bitoffs);
    }

    for (uint_least8_t offs = previous_alternative_bit+1;
         offs < UART_1W_ADDR_LEN*8;
         offs++)
    {
        const uint8_t
            false_presence = !onewire_read(fd),
            true_presence = !onewire_read(fd);

        /* lcd_writepresence(false_presence, */
        /*                   true_presence); */

        const uint8_t byteaddr = (offs & 0xF8) >> 3;
        const uint8_t bitoffs = (offs & 0x07);

        // we must go for false presence first (lower value)
        if (false_presence) {
            addr[byteaddr] &= ~(1<<bitoffs);
            onewire_write0(fd);
        } else if (true_presence) {
            addr[byteaddr] |= (1<<bitoffs);
            onewire_write1(fd);
        }  else {
            // device vanished
            return ONEWIRE_ERROR;
        }

    }

    crc8_state_t state;
    crc8_init(&state, 0x31);
    if (crc8_feed(&state, (const char *)&addr[0], UART_1W_ADDR_LEN) != 0) {
        fprintf(stderr, "crc error while finding device\n");
        return ONEWIRE_CRC_ERROR;
    }

    return ONEWIRE_PRESENCE;
}

uint8_t onewire_address_device(
    int fd,
    const onewire_addr_t addr)
{
    uint8_t status = onewire_reset(fd);
    if (status != ONEWIRE_PRESENCE) {
        return status;
    }
    // MATCH ROM
    onewire_write_byte(fd, 0x55);
    // write addr
    for (uint8_t i = 0; i < UART_1W_ADDR_LEN; i++) {
        onewire_write_byte(fd, addr[i]);
    }

    return status;
}

void onewire_ds18b20_invoke_conversion(
    int fd,
    const onewire_addr_t device)
{
    onewire_address_device(fd, device);
    onewire_write_byte(fd, 0x44);
    usleep(800);
}

uint8_t onewire_read_scratchpad(
    int fd,
    const onewire_addr_t device,
    onewire_scratchpad_t sp)
{
    uint8_t status = onewire_address_device(fd, device);
    if (status != ONEWIRE_PRESENCE) {
        return status;
    }

    onewire_write_byte(fd, 0xBE);

    crc8_state_t state;
    crc8_init(&state, 0x31);
    fprintf(stderr, "reading scratchpad: ");
    for (unsigned int i = 0; i < UART_1W_SCRATCHPAD_LEN; ++i) {
        uint8_t byte = onewire_read_byte(fd);
        fprintf(stderr, "%02x", byte);
        sp[i] = byte;
        crc8_feed(&state, (const char*)&byte, 1);
    }
    if (state.crc != 0) {
        fprintf(stderr, " -- crc fail\n");
        return ONEWIRE_CRC_ERROR;
    }
    fprintf(stderr, " -- crc ok\n");

    return ONEWIRE_PRESENCE;
}

uint8_t onewire_ds18b20_read_temperature(
    int fd,
    const onewire_addr_t device,
    int16_t *Tout)
{
    onewire_scratchpad_t sp;
    uint8_t status = onewire_read_scratchpad(fd, device, sp);
    if (status != ONEWIRE_PRESENCE) {
        return status;
    }

    uint16_t temperature = 0;
    temperature |= sp[0];
    temperature |= ((uint16_t)sp[1] << 8);
    *Tout = (int16_t)temperature;
    return ONEWIRE_PRESENCE;
}

uint8_t onewire_ds18b20_read_temperature_retry(
    int fd,
    const onewire_addr_t device,
    int16_t *Tout,
    unsigned int max_retries)
{
    uint8_t status = onewire_ds18b20_read_temperature(fd, device, Tout);
    uint8_t nattempt = 0;
    while (status == ONEWIRE_CRC_ERROR && nattempt < max_retries) {
        nattempt++;
        fprintf(stderr, "crc error while reading temperature\n");
        usleep(100000);
        fprintf(stderr, "re-trying temperature read\n");
        status = onewire_ds18b20_read_temperature(fd, device, Tout);
    }
    return status;
}

int main(int argc, char **argv)
{
    int fd = open(port, O_RDWR|O_CLOEXEC|O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct termios port_settings;
    memset(&port_settings, 0, sizeof(port_settings));
    speed_t speed = B9600;
    cfsetispeed(&port_settings, speed);
    cfsetospeed(&port_settings, speed);
    cfmakeraw(&port_settings);
    tcsetattr(fd, TCSANOW, &port_settings);
    tcflush(fd, TCIOFLUSH);
    sleep(1);

    fprintf(stderr, "serial initialised\n");

    while (1) {
        onewire_addr_t addr;
        memset(&addr, 0, sizeof(addr));

        while (onewire_findnext(fd, addr) == ONEWIRE_PRESENCE) {
            onewire_ds18b20_invoke_conversion(fd, addr);
            sleep(4);

            for (unsigned i = 0; i < UART_1W_ADDR_LEN; ++i) {
                printf("%02x", addr[i]);
            }

            int16_t raw;
            uint8_t status = onewire_ds18b20_read_temperature_retry(fd, addr, &raw, 2);
            if (status != ONEWIRE_PRESENCE) {
                printf(" read failed (reason=0x%02x)\n",
                       status);
                fprintf(stderr, "read failed (reason=0x%02x)\n");
                sleep(1);
                continue;
            }

            printf(" %f\n", (float)raw/16.0);
            fflush(stdout);
            sleep(4);
        }
    }
}
