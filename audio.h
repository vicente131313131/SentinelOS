#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vfs.h"

// Audio format definitions
#define AUDIO_FORMAT_PCM_8BIT   1
#define AUDIO_FORMAT_PCM_16BIT  2
#define AUDIO_FORMAT_MP3        3

// Audio sample rates
#define AUDIO_SAMPLE_RATE_8KHZ   8000
#define AUDIO_SAMPLE_RATE_11KHZ  11025
#define AUDIO_SAMPLE_RATE_22KHZ  22050
#define AUDIO_SAMPLE_RATE_44KHZ  44100

// Audio buffer sizes
#define AUDIO_BUFFER_SIZE       4096
#define AUDIO_MAX_CHANNELS      2

// WAV file header structure
struct wav_header {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // Format chunk size
    uint16_t audio_format;  // Audio format (1 = PCM)
    uint16_t channels;      // Number of channels
    uint32_t sample_rate;   // Sample rate
    uint32_t byte_rate;     // Byte rate
    uint16_t block_align;   // Block align
    uint16_t bits_per_sample; // Bits per sample
    char data[4];           // "data"
    uint32_t data_size;     // Data size
} __attribute__((packed));

// Audio buffer structure
struct audio_buffer {
    uint8_t* data;
    size_t size;
    size_t position;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t format;
    bool is_playing;
    bool loop;
};

// Audio system state
struct audio_system {
    struct audio_buffer* current_buffer;
    bool initialized;
    bool playing;
    uint32_t volume;        // 0-100
    uint32_t playback_rate; // Current playback sample rate
};

// Function declarations

// System initialization
bool audio_init(void);
void audio_shutdown(void);
void audio_emergency_shutdown(void);

// File loading and parsing
struct audio_buffer* audio_load_wav(struct vfs_node* file);
struct audio_buffer* audio_load_mp3(struct vfs_node* file);
void audio_free_buffer(struct audio_buffer* buffer);

// Playback control
bool audio_play_buffer(struct audio_buffer* buffer);
bool audio_play_file(struct vfs_node* file);
void audio_stop(void);
void audio_pause(void);
void audio_resume(void);
bool audio_is_playing(void);

// Volume and settings
void audio_set_volume(uint32_t volume);
uint32_t audio_get_volume(void);
void audio_set_loop(bool loop);

// Buffer management
struct audio_buffer* audio_create_buffer(size_t size, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
bool audio_mix_buffers(struct audio_buffer* dest, struct audio_buffer* src, float volume);

// Low-level audio output (extends speaker functionality)
void audio_output_sample(int16_t sample);
void audio_output_pcm_data(uint8_t* data, size_t length, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);

// Audio processing helpers
int16_t audio_convert_8bit_to_16bit(uint8_t sample);
void audio_resample(uint8_t* input, size_t input_size, uint32_t input_rate, 
                   uint8_t* output, size_t output_size, uint32_t output_rate);

// Format detection
uint8_t audio_detect_format(struct vfs_node* file);
bool audio_is_wav_file(struct vfs_node* file);
bool audio_is_mp3_file(struct vfs_node* file);

// Global audio system instance
extern struct audio_system g_audio_system;

#endif // AUDIO_H