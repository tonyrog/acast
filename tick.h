#ifndef __TICK_H__
#define __TICK_H__

#include <stdint.h>

typedef uint64_t tick_t;

extern void time_tick_init(void);
extern tick_t time_tick_now(void);
extern tick_t time_tick_from_usec(uint64_t usec);
extern tick_t time_tick_to_usec(tick_t tick);
extern uint64_t time_tick_wait_until(uint64_t end);

#endif
