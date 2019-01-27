#include <unistd.h>
#include <sys/time.h>

#include "tick.h"

#define SPIN_LOCK_USEC 1000ULL  // 1 ms

typedef struct {
    struct timeval tbase;
} time_info_t;

static time_info_t time0;

void time_tick_init()
{
    gettimeofday(&time0.tbase, (struct timezone*) 0);
}

// return absolute time (since program start) in ticks (micros)
tick_t time_tick_now()
{
    struct timeval t1;
    gettimeofday(&t1, (struct timezone*) 0);
    timersub(&t1, &time0.tbase, &t1);
    return t1.tv_sec*1000000 +  t1.tv_usec;
}

tick_t time_tick_from_usec(uint64_t usec)
{
    return usec;
}

tick_t time_tick_to_usec(tick_t tick)
{
    return tick;
}

uint64_t time_tick_wait_until(uint64_t end)
{
    uint64_t now = time_tick_now();
    if (now < end) {
	if (now+SPIN_LOCK_USEC < end) {
	    // printf("usleep %llu\n", (end-now)-SPIN_LOCK_USEC);
	    usleep((end-now)-SPIN_LOCK_USEC);
	}
	while ((now=time_tick_now()) < end);
    }
    return now;
}
