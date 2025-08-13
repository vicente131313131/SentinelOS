#include "audio.h"
#include "vfs.h"
#include "string.h"
#include "serial.h"
#include "speaker.h"
#include "heap.h"
#include "pit.h"
#include "io.h"
#include "libs/minimp3.h"

// System stability protection
static volatile bool audio_system_disabled = false;
static volatile bool audio_emergency_shutdown_flag = false;
static volatile uint32_t audio_watchdog_counter = 0;
static volatile uint32_t audio_stability_failures = 0;
#define MAX_STABILITY_FAILURES 3
#define AUDIO_WATCHDOG_TIMEOUT 1000 // 1 second in milliseconds

// Global audio system state
struct audio_system g_audio_system = {0};

// Audio playback timing
static uint64_t last_sample_time = 0;
static uint32_t samples_per_tick = 0;

// Initialize the audio system
bool audio_init(void) {
    // Check if audio system is globally disabled
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        serial_writestring("Audio system disabled for stability.\n");
        return false;
    }
    
    // Check stability failure count
    if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
        serial_writestring("Audio system disabled due to stability failures.\n");
        audio_system_disabled = true;
        return false;
    }
    
    serial_writestring("Initializing audio system...\n");
    
    // Disable interrupts during initialization
    asm volatile("cli");
    
    g_audio_system.current_buffer = NULL;
    g_audio_system.initialized = true;
    g_audio_system.playing = false;
    g_audio_system.volume = 50; // Default 50% volume
    g_audio_system.playback_rate = AUDIO_SAMPLE_RATE_8KHZ;
    
    // Calculate samples per PIT tick for timing
    samples_per_tick = g_audio_system.playback_rate / 1000; // 1000 Hz PIT
    
    // Reset watchdog
    audio_watchdog_counter = 0;
    
    // Re-enable interrupts
    asm volatile("sti");
    
    serial_writestring("Audio system initialized.\n");
    return true;
}

// Shutdown the audio system
void audio_shutdown(void) {
    // Disable interrupts during shutdown
    asm volatile("cli");
    
    // Stop any playing audio immediately
    pc_speaker_stop();
    
    if (g_audio_system.current_buffer) {
        audio_free_buffer(g_audio_system.current_buffer);
        g_audio_system.current_buffer = NULL;
    }
    
    g_audio_system.initialized = false;
    g_audio_system.playing = false;
    
    // Re-enable interrupts
    asm volatile("sti");
    
    serial_writestring("Audio system shutdown complete.\n");
}

// Emergency shutdown function
void audio_emergency_shutdown(void) {
    audio_emergency_shutdown_flag = true;
    audio_system_disabled = true;
    
    // Immediate hardware shutdown
    pc_speaker_stop();
    
    // Force stop all audio processing
    g_audio_system.playing = false;
    g_audio_system.initialized = false;
    
    serial_writestring("EMERGENCY: Audio system shutdown due to instability!\n");
}

// Create an audio buffer
struct audio_buffer* audio_create_buffer(size_t size, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    struct audio_buffer* buffer = (struct audio_buffer*)kmalloc(sizeof(struct audio_buffer));
    if (!buffer) return NULL;
    
    buffer->data = (uint8_t*)kmalloc(size);
    if (!buffer->data) {
        kfree(buffer);
        return NULL;
    }
    
    buffer->size = size;
    buffer->position = 0;
    buffer->sample_rate = sample_rate;
    buffer->channels = channels;
    buffer->bits_per_sample = bits_per_sample;
    buffer->format = (bits_per_sample == 8) ? AUDIO_FORMAT_PCM_8BIT : AUDIO_FORMAT_PCM_16BIT;
    buffer->is_playing = false;
    buffer->loop = false;
    
    return buffer;
}

// Free an audio buffer
void audio_free_buffer(struct audio_buffer* buffer) {
    if (!buffer) return;
    
    if (buffer->data) {
        kfree(buffer->data);
    }
    kfree(buffer);
}

// Load WAV file
struct audio_buffer* audio_load_wav(struct vfs_node* file) {
    if (!file || !audio_is_wav_file(file)) {
        serial_writestring("Invalid WAV file\n");
        return NULL;
    }
    
    // Read WAV header
    struct wav_header header;
    if (vfs_read(file, 0, sizeof(header), (uint8_t*)&header) != sizeof(header)) {
        serial_writestring("Failed to read WAV header\n");
        return NULL;
    }
    
    // Validate WAV header
    if (memcmp(header.riff, "RIFF", 4) != 0 || 
        memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 ||
        memcmp(header.data, "data", 4) != 0) {
        serial_writestring("Invalid WAV file format\n");
        return NULL;
    }
    
    // Only support PCM format
    if (header.audio_format != 1) {
        serial_writestring("Unsupported WAV format (not PCM)\n");
        return NULL;
    }
    
    // Create audio buffer
    struct audio_buffer* buffer = audio_create_buffer(
        header.data_size, 
        header.sample_rate, 
        header.channels, 
        header.bits_per_sample
    );
    
    if (!buffer) {
        serial_writestring("Failed to create audio buffer\n");
        return NULL;
    }
    
    // Read audio data
    size_t data_offset = sizeof(header);
    if (vfs_read(file, data_offset, header.data_size, buffer->data) != header.data_size) {
        serial_writestring("Failed to read WAV audio data\n");
        audio_free_buffer(buffer);
        return NULL;
    }
    
    serial_writestring("WAV file loaded successfully\n");
    return buffer;
}

// Load MP3 file (stub implementation)
struct audio_buffer* audio_load_mp3(struct vfs_node* file) {
    if (!file || !audio_is_mp3_file(file)) {
        serial_writestring("Invalid MP3 file\n");
        return NULL;
    }
    
    // MP3 support not yet implemented - return error for now
    serial_writestring("MP3 support not yet implemented\n");
    return NULL;
}

// Convert 8-bit sample to 16-bit
int16_t audio_convert_8bit_to_16bit(uint8_t sample) {
    return (int16_t)((sample - 128) * 256);
}

// Output a single PCM sample through the speaker
void audio_output_sample(int16_t sample) {
    // Improved frequency conversion for better audio quality
    // Map 16-bit sample (-32768 to 32767) to frequency range (200-8000 Hz)
    int32_t normalized = sample + 32768; // Convert to 0-65535 range
    uint32_t frequency = 200 + (normalized * 7800) / 65535; // Map to 200-8000 Hz
    
    // Clamp to safe frequency range
    if (frequency < 200) frequency = 200;
    if (frequency > 8000) frequency = 8000;
    
    pc_speaker_play(frequency);
}

// Safe PCM data output with batching to prevent system lockup
void audio_output_pcm_data(uint8_t* data, size_t length, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    // Check system stability first
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        serial_writestring("[Audio] System disabled, skipping playback\n");
        return;
    }
    
    if (!data || length == 0) {
        audio_stability_failures++;
        if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
            audio_emergency_shutdown();
        }
        return;
    }
    
    // Disable interrupts during critical audio processing initialization
    asm volatile("cli");
    
    // Reset watchdog
    audio_watchdog_counter = 0;
    uint64_t start_time = pit_get_ticks();
    
    // Re-enable interrupts
    asm volatile("sti");
    
    // Critical safety limits to prevent system instability
    if (length > 64 * 1024) { // Much smaller limit - 64KB max
        serial_writestring("[Audio] Warning: Large buffer, limiting to 64KB\n");
        length = 64 * 1024;
    }
    
    // Calculate safe batch size (process small chunks at a time)
    const uint32_t MAX_BATCH_SIZE = 25; // Reduced for stability
    const uint32_t MAX_PROCESSING_TIME_MS = 50; // Reduced timeout
    
    uint32_t total_samples = length / (bits_per_sample / 8);
    if (channels > 1) total_samples /= channels;
    
    serial_writestring("[Audio] Safe playback mode: ");
    if (total_samples < 1000) {
        serial_writestring("<1k samples\n");
    } else {
        serial_writestring(">1k samples (batched)\n");
    }
    
    uint32_t samples_processed = 0;
    size_t i = 0;
    
    while (i < length && samples_processed < total_samples) {
        // Watchdog check
        audio_watchdog_counter++;
        if (audio_watchdog_counter > AUDIO_WATCHDOG_TIMEOUT) {
            serial_writestring("[Audio] Watchdog timeout, emergency shutdown\n");
            audio_emergency_shutdown();
            return;
        }
        
        // Emergency exit if processing takes too long
        uint64_t current_time = pit_get_ticks();
        if (current_time - start_time > MAX_PROCESSING_TIME_MS) {
            serial_writestring("[Audio] Emergency timeout - stopping playback\n");
            audio_stability_failures++;
            if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
                audio_emergency_shutdown();
            }
            break;
        }
        
        // Check for emergency shutdown flag
        if (audio_emergency_shutdown_flag) {
            serial_writestring("[Audio] Emergency shutdown detected, exiting\n");
            return;
        }
        
        // Process a small batch of samples
        uint32_t batch_count = 0;
        while (i < length && batch_count < MAX_BATCH_SIZE && samples_processed < total_samples) {
            // Bounds check
            if (i >= length) break;
            
            int16_t sample = 0;
            
            if (bits_per_sample == 8) {
                sample = audio_convert_8bit_to_16bit(data[i]);
                i++;
            } else if (bits_per_sample == 16) {
                if (i + 1 >= length) break;
                sample = *(int16_t*)(data + i);
                i += 2;
            }
            
            // Apply volume safely
            if (g_audio_system.volume > 100) g_audio_system.volume = 100;
            sample = (sample * g_audio_system.volume) / 100;
            
            // Output sample with interrupt protection
            asm volatile("cli");
            audio_output_sample(sample);
            asm volatile("sti");
            
            samples_processed++;
            batch_count++;
            
            // Skip additional channels for mono output
            if (channels > 1) {
                if (bits_per_sample == 16) {
                    if (i + 1 < length) i += 2;
                } else if (bits_per_sample == 8) {
                    if (i < length) i += 1;
                }
            }
        }
        
        // Yield control back to system after each batch
        // Use PIT-based timing instead of busy-wait
        uint64_t batch_end_time = pit_get_ticks();
        while (pit_get_ticks() - batch_end_time < 1) {
            // Very short delay - 1ms between batches
            __asm__ __volatile__("nop; nop; nop"); // Minimal delay instead of hlt
        }
        
        // Progress indicator every 10 batches
        if ((samples_processed / MAX_BATCH_SIZE) % 10 == 0 && samples_processed > 0) {
            serial_writestring("[Audio] Batch progress\n");
        }
        
        // Emergency exit if system becomes unstable
        if (!g_audio_system.playing) {
            serial_writestring("[Audio] Playback stopped by system\n");
            break;
        }
    }
    
    // Reset watchdog on successful completion
    audio_watchdog_counter = 0;
    
    serial_writestring("[Audio] Safe playback completed\n");
}

// Play an audio buffer
bool audio_play_buffer(struct audio_buffer* buffer) {
    // Check system stability first
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        serial_writestring("Audio: System disabled, cannot play buffer\n");
        return false;
    }
    
    if (!buffer || !g_audio_system.initialized) {
        serial_writestring("Audio: Invalid buffer or system not initialized\n");
        audio_stability_failures++;
        if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
            audio_emergency_shutdown();
        }
        return false;
    }
    
    // Check if already playing (prevent concurrent playback)
    if (g_audio_system.playing) {
        serial_writestring("Audio: Already playing, stopping current playback\n");
        audio_stop();
    }
    
    // Disable interrupts during state change
    asm volatile("cli");
    g_audio_system.current_buffer = buffer;
    g_audio_system.playing = true;
    buffer->is_playing = true;
    buffer->position = 0;
    audio_watchdog_counter = 0; // Reset watchdog
    asm volatile("sti");
    
    serial_writestring("Starting audio playback...\n");
    
    // Start playback (this is a blocking implementation for simplicity)
    audio_output_pcm_data(
        buffer->data,
        buffer->size,
        buffer->sample_rate,
        buffer->channels,
        buffer->bits_per_sample
    );
    
    // Playback finished
    asm volatile("cli");
    g_audio_system.playing = false;
    buffer->is_playing = false;
    asm volatile("sti");
    pc_speaker_stop();
    
    serial_writestring("Audio playback finished.\n");
    return true;
}

// Play audio file
bool audio_play_file(struct vfs_node* file) {
    // Check system stability first
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        serial_writestring("Audio: System disabled, cannot play file\n");
        return false;
    }
    
    if (!file || !g_audio_system.initialized) {
        serial_writestring("Audio: Invalid file or system not initialized\n");
        audio_stability_failures++;
        if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
            audio_emergency_shutdown();
        }
        return false;
    }
    
    // Reset watchdog before file operations
    audio_watchdog_counter = 0;
    
    struct audio_buffer* buffer = NULL;
    uint8_t format = audio_detect_format(file);
    
    // Check for emergency shutdown before parsing
    if (audio_emergency_shutdown_flag) {
        serial_writestring("Audio: Emergency shutdown detected, aborting\n");
        return false;
    }
    
    switch (format) {
        case AUDIO_FORMAT_PCM_8BIT:
        case AUDIO_FORMAT_PCM_16BIT:
            buffer = audio_load_wav(file);
            break;
        case AUDIO_FORMAT_MP3:
            buffer = audio_load_mp3(file);
            break;
        default:
            serial_writestring("Unsupported audio format\n");
            audio_stability_failures++;
            return false;
    }
    
    if (!buffer) {
        audio_stability_failures++;
        if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
            audio_emergency_shutdown();
        }
        return false;
    }
    
    // Final stability check before playback
    if (audio_emergency_shutdown_flag) {
        serial_writestring("Audio: Emergency shutdown detected, cleaning up\n");
        audio_free_buffer(buffer);
        return false;
    }
    
    bool result = audio_play_buffer(buffer);
    audio_free_buffer(buffer);
    return result;
}

// Stop audio playback
void audio_stop(void) {
    // Disable interrupts during stop operation
    asm volatile("cli");
    
    // Immediate hardware stop
    pc_speaker_stop();
    
    if (g_audio_system.current_buffer) {
        g_audio_system.current_buffer->is_playing = false;
    }
    g_audio_system.playing = false;
    
    // Reset watchdog
    audio_watchdog_counter = 0;
    
    asm volatile("sti");
    
    serial_writestring("Audio: Playback stopped\n");
}

// Pause audio playback
void audio_pause(void) {
    // Check system stability
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        return;
    }
    
    asm volatile("cli");
    g_audio_system.playing = false;
    pc_speaker_stop();
    asm volatile("sti");
    
    serial_writestring("Audio: Playback paused\n");
}

// Resume audio playback
void audio_resume(void) {
    // Check system stability
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        serial_writestring("Audio: Cannot resume, system disabled\n");
        return;
    }
    
    if (g_audio_system.current_buffer) {
        asm volatile("cli");
        g_audio_system.playing = true;
        audio_watchdog_counter = 0; // Reset watchdog
        asm volatile("sti");
        
        serial_writestring("Audio: Playback resumed\n");
    }
}

// Check if audio is playing
bool audio_is_playing(void) {
    return g_audio_system.playing;
}

// Set volume (0-100)
void audio_set_volume(uint32_t volume) {
    if (volume > 100) volume = 100;
    g_audio_system.volume = volume;
}

// Get current volume
uint32_t audio_get_volume(void) {
    return g_audio_system.volume;
}

// Set loop mode
void audio_set_loop(bool loop) {
    if (g_audio_system.current_buffer) {
        g_audio_system.current_buffer->loop = loop;
    }
}

// Detect audio format
uint8_t audio_detect_format(struct vfs_node* file) {
    if (!file) return 0;
    
    if (audio_is_wav_file(file)) {
        // Read WAV header to determine bit depth
        struct wav_header header;
        if (vfs_read(file, 0, sizeof(header), (uint8_t*)&header) == sizeof(header)) {
            return (header.bits_per_sample == 8) ? AUDIO_FORMAT_PCM_8BIT : AUDIO_FORMAT_PCM_16BIT;
        }
    }
    
    if (audio_is_mp3_file(file)) {
        return AUDIO_FORMAT_MP3;
    }
    
    return 0; // Unknown format
}

// Check if file is WAV
bool audio_is_wav_file(struct vfs_node* file) {
    if (!file || file->length < 12) return false;
    
    char header[12];
    if (vfs_read(file, 0, 12, (uint8_t*)header) != 12) {
        return false;
    }
    
    return (memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0);
}

// Check if file is MP3
bool audio_is_mp3_file(struct vfs_node* file) {
    if (!file || file->length < 3) return false;
    
    char header[3];
    if (vfs_read(file, 0, 3, (uint8_t*)header) != 3) {
        return false;
    }
    
    // Check for MP3 sync word (0xFF 0xFB or 0xFF 0xFA)
    return (header[0] == (char)0xFF && (header[1] == (char)0xFB || header[1] == (char)0xFA));
}

// WAV parsing function with stability protection
struct audio_buffer* audio_parse_wav(uint8_t* data, uint32_t size) {
    // Check system stability first
    if (audio_system_disabled || audio_emergency_shutdown_flag) {
        serial_writestring("Audio: System disabled, cannot parse WAV\n");
        return NULL;
    }
    
    if (!data || size < sizeof(struct wav_header)) {
        serial_writestring("Audio: Invalid WAV data or size too small\n");
        audio_stability_failures++;
        return NULL;
    }
    
    // Reset watchdog during parsing
    audio_watchdog_counter = 0;
    
    struct wav_header* header = (struct wav_header*)data;
    
    // Validate WAV header with bounds checking
    if (memcmp(header, "RIFF", 4) != 0 || 
        memcmp((uint8_t*)header + 8, "WAVE", 4) != 0) {
        serial_writestring("Audio: Invalid WAV header\n");
        audio_stability_failures++;
        return NULL;
    }
    
    // Check for supported format (PCM)
    if (header->audio_format != 1) {
        serial_writestring("Audio: Unsupported audio format (not PCM)\n");
        audio_stability_failures++;
        return NULL;
    }
    
    // Validate audio parameters for stability
    if (header->sample_rate == 0 || header->sample_rate > 48000 ||
        header->channels == 0 || header->channels > 2 ||
        header->bits_per_sample == 0 || header->bits_per_sample > 16) {
        serial_writestring("Audio: Invalid audio parameters\n");
        audio_stability_failures++;
        return NULL;
    }
    
    // Check for emergency shutdown during parsing
    if (audio_emergency_shutdown_flag) {
        serial_writestring("Audio: Emergency shutdown during WAV parsing\n");
        return NULL;
    }
    
    // Find data chunk with bounds checking
    uint8_t* data_ptr = data + sizeof(struct wav_header);
    uint32_t data_size = header->data_size;
    
    // Validate data size
    if (data_size == 0 || data_size > 64 * 1024) { // 64KB limit
        serial_writestring("Audio: Invalid or excessive data chunk size\n");
        audio_stability_failures++;
        return NULL;
    }
    
    // Final emergency check before allocation
    if (audio_emergency_shutdown_flag) {
        serial_writestring("Audio: Emergency shutdown before allocation\n");
        return NULL;
    }
    
    // Allocate buffer with error checking
    struct audio_buffer* buffer = (struct audio_buffer*)kmalloc(sizeof(struct audio_buffer));
    if (!buffer) {
        serial_writestring("Audio: Failed to allocate buffer\n");
        audio_stability_failures++;
        if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
            audio_emergency_shutdown();
        }
        return NULL;
    }
    
    // Allocate data buffer with size validation
    buffer->data = (uint8_t*)kmalloc(data_size);
    if (!buffer->data) {
        serial_writestring("Audio: Failed to allocate data buffer\n");
        kfree(buffer);
        audio_stability_failures++;
        if (audio_stability_failures >= MAX_STABILITY_FAILURES) {
            audio_emergency_shutdown();
        }
        return NULL;
    }
    
    // Copy audio data with bounds checking
    if (data_ptr + data_size <= data + size) {
        memcpy(buffer->data, data_ptr, data_size);
    } else {
        serial_writestring("Audio: Data bounds check failed\n");
        kfree(buffer->data);
        kfree(buffer);
        audio_stability_failures++;
        return NULL;
    }
    
    // Set buffer properties
    buffer->size = data_size;
    buffer->sample_rate = header->sample_rate;
    buffer->channels = header->channels;
    buffer->bits_per_sample = header->bits_per_sample;
    buffer->is_playing = false;
    buffer->position = 0;
    
    // Reset watchdog after successful parsing
    audio_watchdog_counter = 0;
    
    serial_writestring("Audio: WAV file parsed successfully\n");
    return buffer;
}

// Basic buffer mixing (for future use)
bool audio_mix_buffers(struct audio_buffer* dest, struct audio_buffer* src, float volume) {
    if (!dest || !src || !dest->data || !src->data) return false;
    
    // Simple mixing implementation
    size_t mix_size = (dest->size < src->size) ? dest->size : src->size;
    
    for (size_t i = 0; i < mix_size; i += 2) { // Assuming 16-bit samples
        int16_t dest_sample = *(int16_t*)(dest->data + i);
        int16_t src_sample = *(int16_t*)(src->data + i);
        
        int32_t mixed = dest_sample + (int32_t)(src_sample * volume);
        
        // Clamp to 16-bit range
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        
        *(int16_t*)(dest->data + i) = (int16_t)mixed;
    }
    
    return true;
}