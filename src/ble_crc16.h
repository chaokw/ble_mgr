#ifndef __CRC16_H
#define __CRC16_H
#include <stdint.h>

extern uint16_t const crc16_table[256];
extern uint16_t ble_crc16(uint16_t crc, const uint8_t *buffer, uint16_t len);

#endif /* __CRC16_H */
