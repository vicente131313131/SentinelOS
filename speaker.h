#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>
#include <stddef.h>

// Start the PC speaker at a given frequency (in Hz)
void pc_speaker_play(uint32_t frequency);

// Stop the PC speaker
void pc_speaker_stop(void);

// Convenience: play a tone for duration_ms milliseconds
void beep(uint32_t frequency, uint32_t duration_ms);

// Enhanced PCM audio output functions
void pc_speaker_play_pcm_sample(int16_t sample);
void pc_speaker_play_pcm_buffer(uint8_t* buffer, size_t length, uint32_t sample_rate, uint16_t bits_per_sample);
void delay_microseconds(uint32_t microseconds);
void pc_speaker_play_tone_advanced(uint32_t frequency, uint32_t duration_ms, uint8_t volume);

#endif

