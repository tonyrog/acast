#ifndef __CRCR32_H__
#define __CRCR32_H__

#include <stdint.h>

extern uint32_t crc32_init(void);
extern uint32_t crc32_final(uint32_t crc);
extern uint32_t crc32_update(uint32_t crc, uint8_t* ptr, size_t len);
extern uint32_t crc32(uint8_t* ptr, size_t len);

#endif
