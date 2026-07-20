#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void init_timer(uint32_t frequency);
uint32_t get_ticks(void);
void sleep_ms(uint32_t ms);

#endif
