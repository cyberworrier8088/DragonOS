#ifndef RTC_H
#define RTC_H

#include <stdint.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

void rtc_read_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds);

#endif
