#ifndef __G711_H__
#define __G711_H__

#include <stdint.h>

extern uint8_t linear2alaw(int16_t pcm_val);
extern int16_t alaw2linear(uint8_t a_val);
extern uint8_t linear2ulaw(int16_t pcm_val);
extern int16_t ulaw2linear(uint8_t u_val);

#endif
