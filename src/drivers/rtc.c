#include "rtc.h"
#include "../cpu/ports.h"

static int get_update_in_progress_flag(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

void rtc_read_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds) {
    // Wait until update in progress flag is clear
    for (volatile int timeout = 0; timeout < 10000; timeout++) {
        if (!get_update_in_progress_flag()) break;
    }
    
    uint8_t sec = get_rtc_register(0x00);
    uint8_t min = get_rtc_register(0x02);
    uint8_t hour = get_rtc_register(0x04);
    
    uint8_t registerB = get_rtc_register(0x0B);
    
    // Convert BCD to binary if necessary
    if (!(registerB & 0x04)) {
        sec = (sec & 0x0F) + ((sec / 16) * 10);
        min = (min & 0x0F) + ((min / 16) * 10);
        hour = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
    }
    
    // Convert 12-hour clock to 24-hour clock if necessary
    if (!(registerB & 0x02)) {
        int pm = hour & 0x80;
        hour &= 0x7F;
        if (pm) {
            if (hour != 12) hour += 12;
        } else {
            if (hour == 12) hour = 0;
        }
    }
    
    *seconds = sec;
    *minutes = min;
    *hours = hour;
}

void rtc_read_date(uint8_t* year, uint8_t* month, uint8_t* day) {
    // Wait until update in progress flag is clear
    for (volatile int timeout = 0; timeout < 10000; timeout++) {
        if (!get_update_in_progress_flag()) break;
    }

    uint8_t d = get_rtc_register(0x07); // Day of month
    uint8_t m = get_rtc_register(0x08); // Month
    uint8_t y = get_rtc_register(0x09); // Year (two digits)

    uint8_t registerB = get_rtc_register(0x0B);

    // Convert BCD to binary if the RTC is not already in binary mode
    if (!(registerB & 0x04)) {
        d = (d & 0x0F) + ((d / 16) * 10);
        m = (m & 0x0F) + ((m / 16) * 10);
        y = (y & 0x0F) + ((y / 16) * 10);
    }

    *day = d;
    *month = m;
    *year = y;
}
