#include <stdint.h>
#include "io.h"
#include "pit.h"
#include "speaker.h"

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


