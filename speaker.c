#include <stdint.h>
#include "io.h"
#include "pit.h"
#include "speaker.h"
#include "audio.h"

// PIT channel 2 is used for the PC speaker
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43
#define SPEAKER_PORT    0x61

static inline void pit_set_channel2(uint32_t frequency)
{
    if (frequency == 0) frequency = 1;
    uint32_t divisor = 1193180 / frequency;
    if (divisor == 0) divisor = 1;
    // Set channel 2, lobyte/hibyte, mode 3 (square wave), binary
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF));
}

void pc_speaker_play(uint32_t frequency)
{
    pit_set_channel2(frequency);
    // Enable speaker by setting bits 0 and 1 on port 0x61
    uint8_t tmp = inb(SPEAKER_PORT);
    if ((tmp & 3) != 3) {
        outb(SPEAKER_PORT, tmp | 3);
    }
}

void pc_speaker_stop(void)
{
    // Disable speaker (clear bits 0 and 1)
    uint8_t tmp = inb(SPEAKER_PORT) & 0xFC;
    outb(SPEAKER_PORT, tmp);
}

// Very rough delay; use existing delay from kernel.c via weak symbol if available
extern void delay(int milliseconds);

void beep(uint32_t frequency, uint32_t duration_ms)
{
    if (frequency == 0) frequency = 1000; // default 1 kHz
    if (duration_ms == 0) duration_ms = 200; // default 200 ms
    pc_speaker_play(frequency);
    delay((int)duration_ms);
    pc_speaker_stop();
}

// Enhanced PCM audio output functions
void pc_speaker_play_pcm_sample(int16_t sample)
{
    // Convert PCM sample to frequency
    // Map 16-bit signed sample (-32768 to 32767) to frequency range
    uint32_t base_freq = 1000; // Base frequency in Hz
    int32_t freq_offset = sample / 16; // Scale down the sample
    int32_t frequency = base_freq + freq_offset;
    
    // Clamp frequency to reasonable range
    if (frequency < 100) frequency = 100;
    if (frequency > 10000) frequency = 10000;
    
    pc_speaker_play((uint32_t)frequency);
}

void pc_speaker_play_pcm_buffer(uint8_t* buffer, size_t length, uint32_t sample_rate, uint16_t bits_per_sample)
{
    if (!buffer || length == 0) return;
    
    // Calculate microseconds per sample
    uint32_t us_per_sample = 1000000 / sample_rate;
    
    for (size_t i = 0; i < length; ) {
        int16_t sample = 0;
        
        if (bits_per_sample == 8) {
            // Convert 8-bit unsigned to 16-bit signed
            uint8_t sample_8 = buffer[i];
            sample = (int16_t)((sample_8 - 128) * 256);
            i++;
        } else if (bits_per_sample == 16) {
            // Use 16-bit sample directly
            sample = *(int16_t*)(buffer + i);
            i += 2;
        } else {
            i++; // Skip unsupported bit depths
            continue;
        }
        
        // Play the sample
        pc_speaker_play_pcm_sample(sample);
        
        // Timing delay (very basic)
        delay_microseconds(us_per_sample);
    }
}

// Microsecond delay function for audio timing
void delay_microseconds(uint32_t microseconds)
{
    // Very rough microsecond delay using busy wait
    // This is not accurate but works for basic audio timing
    volatile uint32_t count = microseconds * 10; // Rough calibration
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

// Play a tone with specific duration and fade
void pc_speaker_play_tone_advanced(uint32_t frequency, uint32_t duration_ms, uint8_t volume)
{
    if (frequency == 0) return;
    if (volume > 100) volume = 100;
    
    // Simple volume control by modulating the speaker on/off time
    uint32_t on_time = (volume * 10) / 100; // 0-10 units
    uint32_t off_time = 10 - on_time;
    
    pit_set_channel2(frequency);
    
    uint64_t start_time = pit_get_ticks();
    uint64_t end_time = start_time + duration_ms;
    
    while (pit_get_ticks() < end_time) {
        // Enable speaker
        uint8_t tmp = inb(SPEAKER_PORT);
        outb(SPEAKER_PORT, tmp | 3);
        
        // On time
        for (volatile uint32_t i = 0; i < on_time * 1000; i++) {
            __asm__ __volatile__("nop");
        }
        
        // Disable speaker
        tmp = inb(SPEAKER_PORT) & 0xFC;
        outb(SPEAKER_PORT, tmp);
        
        // Off time
        for (volatile uint32_t i = 0; i < off_time * 1000; i++) {
            __asm__ __volatile__("nop");
        }
    }
    
    pc_speaker_stop();
}


