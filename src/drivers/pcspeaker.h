#ifndef PCSPEAKER_H
#define PCSPEAKER_H

#include <stdint.h>

// PC Speaker driver: square-wave tone generation via PIT channel 2, gated
// through the keyboard-controller port (0x61). Uses PIT channel 2 only, so it
// does not disturb the system timer on channel 0.

void pcspeaker_play(uint32_t freq); // start a continuous tone at freq Hz
void pcspeaker_stop(void);          // silence the speaker
void pcspeaker_beep(uint32_t freq, uint32_t ms); // tone for ms milliseconds

#endif
