#ifndef RTC_H
#define RTC_H

#include <stdint.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

void rtc_read_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds);
void rtc_read_date(uint8_t* year, uint8_t* month, uint8_t* day);

#endif
