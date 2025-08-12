#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

// Start the PC speaker at a given frequency (in Hz)
void pc_speaker_play(uint32_t frequency);

// Stop the PC speaker
void pc_speaker_stop(void);

// Convenience: play a tone for duration_ms milliseconds
void beep(uint32_t frequency, uint32_t duration_ms);

#endif

