#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "string.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "serial.h"
#include "keyboard.h"
#include "SpringIntoView/spring_into_view.h"
#include "multiboot2.h"
#include "initrd.h"
#include "vfs.h"
#include "pmm.h"
#include "heap.h"
#include "vmm.h"
#include "mouse.h"

#include "vbe.h"
#include "io.h"

struct framebuffer_info {
    uint32_t width;
    uint32_t height;
};

static inline void sti() { __asm__ __volatile__ ("sti"); }

struct multiboot2_tag_framebuffer *find_framebuffer_tag(struct multiboot2_info *mbi) {
    for (struct multiboot2_tag *tag = (struct multiboot2_tag *)((uint8_t*)mbi + 8);
         tag->type != MULTIBOOT2_TAG_TYPE_END;
         tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER) {
            return (struct multiboot2_tag_framebuffer *)tag;
        }
    }
    return NULL;
}

struct multiboot2_tag_module *find_module_tag(struct multiboot2_info *mbi, const char* name) {
    struct multiboot2_tag_module* first_module = NULL;
    for (struct multiboot2_tag *tag = (struct multiboot2_tag *)((uint8_t*)mbi + 8);
         tag->type != MULTIBOOT2_TAG_TYPE_END;
         tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
            struct multiboot2_tag_module *mod = (struct multiboot2_tag_module *)tag;
            if (!first_module) first_module = mod;
            const char* cmd = mod->cmdline;
            // Accept both exact name and basename match (e.g., "/boot/initrd.tar").
            // Note: Some GRUB configs leave cmdline empty; we handle that by falling back below.
            if (name && name[0]) {
                const char* base = strrchr(cmd, '/');
                base = base ? (base + 1) : cmd;
                if ((cmd && strcmp(cmd, name) == 0) || (base && strcmp(base, name) == 0)) {
                    return mod;
                }
            }
        }
    }
    // Fallback: return the first module if no named match was found
    return first_module;
}

struct multiboot2_tag_mmap *find_mmap_tag(struct multiboot2_info *mbi) {
    for (struct multiboot2_tag *tag = (struct multiboot2_tag *)((uint8_t*)mbi + 8);
         tag->type != MULTIBOOT2_TAG_TYPE_END;
         tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            return (struct multiboot2_tag_mmap *)tag;
        }
    }
    return NULL;
}

struct multiboot2_tag_vbe *find_vbe_tag(struct multiboot2_info *mbi) {
    for (struct multiboot2_tag *tag = (struct multiboot2_tag *)((uint8_t*)mbi + 8);
         tag->type != MULTIBOOT2_TAG_TYPE_END;
         tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_VBE) {
            return (struct multiboot2_tag_vbe *)tag;
        }
    }
    return NULL;
}

// VGA text mode colors
#define VGA_BLACK 0
#define VGA_BLUE 1
#define VGA_GREEN 2
#define VGA_CYAN 3
#define VGA_RED 4
#define VGA_MAGENTA 5
#define VGA_BROWN 6
#define VGA_LIGHT_GREY 7
#define VGA_DARK_GREY 8
#define VGA_LIGHT_BLUE 9
#define VGA_LIGHT_GREEN 10
#define VGA_LIGHT_CYAN 11
#define VGA_LIGHT_RED 12
#define VGA_LIGHT_MAGENTA 13
#define VGA_LIGHT_BROWN 14
#define VGA_WHITE 15

// VGA text mode dimensions
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// VGA text mode buffer address
#define VGA_BUFFER 0xB8000

// Terminal state
static uint16_t* terminal_buffer = (uint16_t*)VGA_BUFFER;
static size_t terminal_row = 0;
static size_t terminal_column = 0;
static uint8_t terminal_color = VGA_LIGHT_GREY | VGA_BLACK << 4;
static bool cursor_visible = true;  // Track cursor visibility state
static uint16_t saved_cursor_entry = 0;

// Shell buffer and history
#define SHELL_BUFFER_SIZE 128
#define HISTORY_SIZE 10
static char shell_buffer[SHELL_BUFFER_SIZE];
static size_t shell_buffer_len = 0;
static size_t shell_cursor_pos = 0;
static size_t shell_sel_anchor = (size_t)-1; // selection anchor index or (size_t)-1 when none
static char clipboard[SHELL_BUFFER_SIZE];
static size_t shell_prompt_col = 0; // column where input starts on the current line
static char command_history[HISTORY_SIZE][SHELL_BUFFER_SIZE];
static int history_count = 0;
static int history_index = 0;
static int current_history_view = -1;

static struct vfs_node* cwd = NULL;

// Global variable to hold the Multiboot 2 info address
static uint64_t g_mb2_info_addr = 0;
// Framebuffer runtime state (used by graphics overlay in Bochs/QEMU after vbeset)
// duplicate removed
// static copies exist later; remove these duplicates

// Function declarations
static inline uint16_t vga_entry(unsigned char uc, uint8_t color);
void terminal_set_color(uint8_t color);
void disable_hw_cursor(void);
void terminal_initialize(void);
void terminal_put_entry_at(char c, uint8_t color, size_t x, size_t y);
void terminal_scroll(void);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_writestring_utf8(const char* data);
void get_cwd_path(char* buffer, size_t size);
void shell_prompt();
void shell_clear();
void shell_handle_command(const char* cmd);
void shell_input_char(char c);
void draw_cursor(void);
void erase_cursor(void);
void delay(int milliseconds);
void init_graphics(struct multiboot2_tag_framebuffer* fb_tag);
void update_progress_bar(int percentage, const char* text);
void draw_progress_bar_background(void);
void init_terminal(void);
void terminal_writehex(uint64_t n);
void terminal_writedec(size_t n);

static void shell_redraw_line_with_selection(void) {
    // Move cursor to start of input (after prompt)
    size_t back = (terminal_column > shell_prompt_col) ? (terminal_column - shell_prompt_col) : 0;
    for (size_t i = 0; i < back; i++) terminal_putchar('\b');

    // Determine selection range
    size_t sel_start = (size_t)-1, sel_end = (size_t)-1;
    if (shell_sel_anchor != (size_t)-1 && shell_sel_anchor != shell_cursor_pos) {
        sel_start = (shell_sel_anchor < shell_cursor_pos) ? shell_sel_anchor : shell_cursor_pos;
        sel_end   = (shell_sel_anchor < shell_cursor_pos) ? shell_cursor_pos : shell_sel_anchor;
    }

    // Draw buffer with inverted colors for selection
    uint8_t normal = terminal_color;
    uint8_t inverted = ((normal & 0xF) << 4) | ((normal >> 4) & 0xF);
    for (size_t i = 0; i < shell_buffer_len; i++) {
        uint8_t color = normal;
        if (sel_start != (size_t)-1 && i >= sel_start && i < sel_end) {
            color = inverted;
        }
        terminal_put_entry_at(shell_buffer[i], color, shell_prompt_col + i, terminal_row);
    }
    // Erase next cell to clear leftovers when line shrinks
    terminal_put_entry_at(' ', normal, shell_prompt_col + shell_buffer_len, terminal_row);

    // Move hardware cursor to logical cursor position
    terminal_column = shell_prompt_col + shell_cursor_pos;
    draw_cursor();
}

// (moved runtime graphics init below fb_info and graphics_initialized definitions)

// Supported shell commands for autocomplete
static const char* SHELL_COMMANDS[] = {
    "help", "clear", "echo", "info", "graphics", "ls", "cat", "touch", "rm",
    "mkdir", "cd", "pwd", "meminfo", "heapinfo", "vbeinfo", "savefs"
};
static const size_t NUM_SHELL_COMMANDS = sizeof(SHELL_COMMANDS) / sizeof(SHELL_COMMANDS[0]);

// Function to create a VGA entry
static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | (uint16_t)color << 8;
}

// Function to draw the cursor at current position
void draw_cursor(void) {
    if (cursor_visible) {
        size_t index = terminal_row * VGA_WIDTH + terminal_column;
        saved_cursor_entry = terminal_buffer[index];
        uint8_t inverted_color = ((terminal_color & 0xF) << 4) | ((terminal_color >> 4) & 0xF);
        terminal_buffer[index] = vga_entry('_', inverted_color);
    }
}

// Function to erase the cursor from current position
void erase_cursor(void) {
    terminal_buffer[terminal_row * VGA_WIDTH + terminal_column] = saved_cursor_entry;
}

// Function to set terminal color
void terminal_set_color(uint8_t color) {
    erase_cursor();
    terminal_color = color;
    draw_cursor();
}

void disable_hw_cursor(void) {
    // Disable the VGA hardware cursor by setting the cursor start register's bit 5
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

// Function to initialize terminal
void terminal_initialize(void) {
    cursor_visible = false;
    disable_hw_cursor();
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = VGA_LIGHT_GREY | VGA_BLACK << 4;
    
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
    
    cursor_visible = true;
}

// Function to put a character at a specific position
void terminal_put_entry_at(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, color);
}

// Function to scroll the terminal
void terminal_scroll(void) {
    erase_cursor();
    
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            const size_t next_index = (y + 1) * VGA_WIDTH + x;
            terminal_buffer[index] = terminal_buffer[next_index];
        }
    }
    
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = vga_entry(' ', terminal_color);
    }
    
    draw_cursor();
}

// Function to put a character
void terminal_putchar(char c) {
    erase_cursor();
    
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
        draw_cursor();
        return;
    }
    
    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            terminal_put_entry_at(' ', terminal_color, terminal_column, terminal_row);
        }
        draw_cursor();
        return;
    }
    
    terminal_put_entry_at(c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
    }
    draw_cursor();
}

// Function to write a string
void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

// Function to write a null-terminated string (raw bytes)
void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
    }
}

// UTF-8 -> CP437 best-effort printer for common Latin-1 accents
static unsigned char cp437_from_unicode(int cp)
{
    switch (cp) {
        case 0x00E1: return (unsigned char)0xA0; // á
        case 0x00E9: return (unsigned char)0x82; // é
        case 0x00ED: return (unsigned char)0xA1; // í
        case 0x00F3: return (unsigned char)0xA2; // ó
        case 0x00FA: return (unsigned char)0xA3; // ú
        case 0x00F1: return (unsigned char)0xA4; // ñ
        case 0x00FC: return (unsigned char)0x81; // ü
        case 0x00C1: return (unsigned char)0xB5; // Á
        case 0x00C9: return (unsigned char)0x90; // É
        case 0x00CD: return (unsigned char)0xD6; // Í
        case 0x00D3: return (unsigned char)0xE0; // Ó
        case 0x00DA: return (unsigned char)0xE9; // Ú
        case 0x00D1: return (unsigned char)0xA5; // Ñ
        case 0x00DC: return (unsigned char)0x9A; // Ü
        case 0x00BF: return (unsigned char)0xA8; // ¿
        case 0x00A1: return (unsigned char)0xAD; // ¡
        default: return '?';
    }
}

static int utf8_decode_advance_tm(const char** p)
{
    const unsigned char* s = (const unsigned char*)(*p);
    if (!s || *s == 0) return -1;
    unsigned char b0 = s[0];
    if (b0 < 0x80) { (*p) += 1; return b0; }
    if ((b0 & 0xE0) == 0xC0) {
        unsigned char b1 = s[1]; if ((b1 & 0xC0) != 0x80) { (*p)+=1; return 0xFFFD; }
        int cp = ((int)(b0 & 0x1F) << 6) | (int)(b1 & 0x3F);
        if (cp < 0x80) cp = 0xFFFD; (*p)+=2; return cp;
    }
    if ((b0 & 0xF0) == 0xE0) {
        unsigned char b1 = s[1], b2 = s[2]; if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { (*p)+=1; return 0xFFFD; }
        int cp = ((int)(b0 & 0x0F) << 12) | ((int)(b1 & 0x3F) << 6) | (int)(b2 & 0x3F);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD; (*p)+=3; return cp;
    }
    if ((b0 & 0xF8) == 0xF0) {
        unsigned char b1 = s[1], b2 = s[2], b3 = s[3]; if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { (*p)+=1; return 0xFFFD; }
        int cp = ((int)(b0 & 0x07) << 18) | ((int)(b1 & 0x3F) << 12) | ((int)(b2 & 0x3F) << 6) | (int)(b3 & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD; (*p)+=4; return cp;
    }
    (*p)+=1; return 0xFFFD;
}

void terminal_writestring_utf8(const char* data)
{
    const char* p = data;
    while (1) {
        int cp = utf8_decode_advance_tm(&p);
        if (cp < 0) break;
        if (cp < 0x80) {
            terminal_putchar((char)cp);
        } else {
            unsigned char ch = cp437_from_unicode(cp);
            terminal_putchar((char)ch);
        }
    }
}

void get_cwd_path(char* buffer, size_t size) {
    if (cwd == vfs_root) {
        strcpy(buffer, "/");
        return;
    }

    buffer[0] = '\0';
    char temp[256];
    struct vfs_node* current = cwd;

    while (current && current->parent) {
        strcpy(temp, buffer);
        strcpy(buffer, current->name);
        if (strlen(temp) > 0) {
            strcat(buffer, "/");
            strcat(buffer, temp);
        }
        current = current->parent;
    }
    
    strcpy(temp, buffer);
    strcpy(buffer, "/");
    strcat(buffer, temp);
}

void shell_prompt() {
    shell_cursor_pos = 0;
    char path_buf[256];
    get_cwd_path(path_buf, 256);
    terminal_writestring(path_buf);
    terminal_writestring("> ");
    shell_prompt_col = terminal_column; // remember where input starts
}

void shell_clear() {
    terminal_initialize();
    shell_prompt();
}

void shell_handle_command(const char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        terminal_writestring("Available commands:\n");
        terminal_writestring(" - help: Display this help message\n");
        terminal_writestring(" - clear: Clear the screen\n");
        terminal_writestring(" - echo <text>: Print text\n");
        terminal_writestring(" - info: Show OS info\n");
        terminal_writestring(" - graphics: Enter graphics mode\n");
        terminal_writestring(" - ls [path]: List files\n");
        terminal_writestring(" - cat <file>: Read file\n");
        terminal_writestring(" - touch <file>: Create file\n");
        terminal_writestring(" - rm <file>: Delete file\n");
        terminal_writestring(" - mkdir <dir>: Create directory\n");
        terminal_writestring(" - cd <dir>: Change directory\n");
        terminal_writestring(" - pwd: Print working directory\n");
        terminal_writestring(" - meminfo: Show memory info\n");
        terminal_writestring(" - heapinfo: Show heap info\n");
        terminal_writestring(" - vbeinfo: Show VBE info\n");
        terminal_writestring(" - savefs: Dump current VFS as a tar stream over serial\n");
        // vbeset is disabled while under development
    } else if (strcmp(cmd, "clear") == 0) {
        shell_clear();
        // removed 'sent' command
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        terminal_writestring(cmd + 5);
        terminal_writestring("\n");
    } else if (strcmp(cmd, "ls") == 0 || strncmp(cmd, "ls ", 3) == 0) {
        const char* path = (strcmp(cmd, "ls") == 0) ? "." : cmd + 3;
        struct vfs_node* node_to_ls = vfs_path_lookup(cwd, path);
        
        if (node_to_ls && (node_to_ls->flags & VFS_DIRECTORY)) {
            struct dirent* de;
            uint32_t i = 0;
            while ((de = vfs_readdir(node_to_ls, i++)) != NULL) {
                if(de->name[0] != '\0') {
                    terminal_writestring(de->name);
                    terminal_writestring("\n");
                }
            }
        } else {
            terminal_writestring("ls: not a directory or does not exist.\n");
        }
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        const char* filename = cmd + 4;
        struct vfs_node* file_node = vfs_path_lookup(cwd, filename);
        if (file_node && (file_node->flags & VFS_FILE)) {
            char* buffer = (char*)0x10000; // a safe place to read
            size_t bytes_read = vfs_read(file_node, 0, file_node->length, (uint8_t*)buffer);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                terminal_writestring(buffer);
            }
        } else {
            terminal_writestring("cat: file not found or is a directory\n");
        }
    } else if (strncmp(cmd, "touch ", 6) == 0) {
        const char* path = cmd + 6;
        char path_buf[256];
        strcpy(path_buf, path);

        char* basename = path_buf;
        struct vfs_node* parent_node = cwd;

        char* last_slash = strrchr(path_buf, '/');
        if (last_slash) {
            basename = last_slash + 1;
            if (last_slash == path_buf) {
                parent_node = vfs_root;
            } else {
                *last_slash = '\0';
                parent_node = vfs_path_lookup(cwd, path_buf);
            }
        }
        
        if (parent_node && (parent_node->flags & VFS_DIRECTORY)) {
            struct vfs_node* new_file = vfs_create(parent_node, basename, VFS_FILE);
            if (!new_file) {
                terminal_writestring("Failed to create file.\n");
            }
        } else {
            terminal_writestring("touch: path not found.\n");
        }
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        const char* path = cmd + 3;
        char path_buf[256];
        strcpy(path_buf, path);

        char* basename = path_buf;
        struct vfs_node* parent_node = cwd;

        char* last_slash = strrchr(path_buf, '/');
        if (last_slash) {
            basename = last_slash + 1;
            if (last_slash == path_buf) {
                parent_node = vfs_root;
            } else {
                *last_slash = '\0';
                parent_node = vfs_path_lookup(cwd, path_buf);
            }
        }
        
        if (parent_node && (parent_node->flags & VFS_DIRECTORY)) {
            if (vfs_delete(parent_node, basename) != 0) {
                terminal_writestring("Failed to delete file.\n");
            }
        } else {
            terminal_writestring("rm: path not found.\n");
        }
    } else if (strcmp(cmd, "info") == 0) {
        terminal_writestring_utf8("SentinelOS by Vicente Velásquez\n");
    } else if (strcmp(cmd, "graphics") == 0) {
        terminal_writestring("Graphics mode is only available at boot.\n");
    } else if (strncmp(cmd, "vbeset", 6) == 0) {
        terminal_writestring("vbeset is currently disabled (WIP).\n");
        goto after_cmd;
    } else if (strcmp(cmd, "savefs") == 0) {
        // Stream a minimal ustar tar over serial of the current VFS
        // Only regular files and directories with simple names (<= 100)
        void tar_write_padding(size_t n) {
            while (n--) serial_write('\0');
        }
        void tar_write_octal(char* dst, size_t size, size_t value) {
            // size includes trailing NUL; pad with leading zeros
            for (int i = (int)size - 2; i >= 0; --i) { dst[i] = '0' + (value % 8); value /= 8; }
            dst[size - 1] = '\0';
        }
        void tar_write_header(const char* name, size_t size, char typeflag) {
            char hdr[512];
            memset(hdr, 0, sizeof(hdr));
            // name
            size_t namelen = strlen(name);
            if (namelen > 100) namelen = 100; // truncate
            memcpy(hdr + 0, name, namelen);
            // mode, uid, gid
            tar_write_octal(hdr + 100, 8, 0644);
            tar_write_octal(hdr + 108, 8, 0);
            tar_write_octal(hdr + 116, 8, 0);
            // size
            tar_write_octal(hdr + 124, 12, (typeflag == '0') ? size : 0);
            // mtime
            tar_write_octal(hdr + 136, 12, 0);
            // chksum field initially spaces
            memset(hdr + 148, ' ', 8);
            hdr[156] = typeflag; // typeflag
            memcpy(hdr + 257, "ustar\0", 6);
            memcpy(hdr + 263, "00", 2);
            // checksum
            unsigned int sum = 0;
            for (int i = 0; i < 512; ++i) sum += (unsigned char)hdr[i];
            tar_write_octal(hdr + 148, 8, sum);
            for (int i = 0; i < 512; ++i) serial_write(hdr[i]);
        }
        void tar_dump_node(struct vfs_node* node, const char* path_prefix) {
            char path[256];
            // Build path
            if (path_prefix[0] == '\0' || (path_prefix[0] == '/' && path_prefix[1] == '\0')) {
                // root prefix
                if (strcmp(node->name, "/") == 0) {
                    // Emit root directory as "." entry
                    tar_write_header(".", 0, '5');
                } else {
                    strcpy(path, node->name);
                    tar_write_header(path, 0, (node->flags & VFS_DIRECTORY) ? '5' : '0');
                }
            } else {
                strcpy(path, path_prefix);
                size_t n = strlen(path);
                if (n > 0 && path[n-1] != '/') strcat(path, "/");
                strcat(path, node->name);
                tar_write_header(path, (node->flags & VFS_DIRECTORY) ? 0 : node->length, (node->flags & VFS_DIRECTORY) ? '5' : '0');
            }

            if (node->flags & VFS_FILE) {
                // Write file data in 512-byte blocks
                size_t remaining = node->length;
                size_t offset = 0;
                char block[512];
                while (remaining > 0) {
                    size_t chunk = remaining > 512 ? 512 : remaining;
                    memset(block, 0, sizeof(block));
                    vfs_read(node, offset, chunk, (uint8_t*)block);
                    for (size_t i = 0; i < 512; ++i) serial_write(block[i]);
                    offset += chunk;
                    remaining -= chunk;
                }
            }

            if (node->flags & VFS_DIRECTORY) {
                // Iterate children
                struct vfs_node* child = node->first_child;
                while (child) {
                    tar_dump_node(child, (strcmp(node->name, "/") == 0) ? "/" : path);
                    child = child->next_sibling;
                }
            }
        }
        // Kick off from root
        if (!vfs_root) { terminal_writestring("VFS not mounted.\n"); goto after_cmd; }
        serial_writestring("[savefs] Begin TAR on serial...\n");
        tar_dump_node(vfs_root, "/");
        // Two 512-byte zero blocks to end archive
        for (int i = 0; i < 1024; ++i) serial_write('\0');
        serial_writestring("[savefs] End TAR.\n");
    } else if (strncmp(cmd, "mkdir ", 6) == 0) {
        const char* path = cmd + 6;
        char path_buf[256];
        strcpy(path_buf, path);

        char* basename = path_buf;
        struct vfs_node* parent_node = cwd;

        char* last_slash = strrchr(path_buf, '/');
        if (last_slash) {
            basename = last_slash + 1;
            if (last_slash == path_buf) {
                parent_node = vfs_root;
            } else {
                *last_slash = '\0';
                parent_node = vfs_path_lookup(cwd, path_buf);
            }
        }
        
        if (parent_node && (parent_node->flags & VFS_DIRECTORY)) {
            // If directory already exists, do nothing
            extern struct vfs_node* finddir_initrd(struct vfs_node*, char*);
            if (finddir_initrd(parent_node, basename)) {
                terminal_writestring("Directory already exists.\n");
                goto after_cmd;
            }
            struct vfs_node* new_dir = vfs_create(parent_node, basename, VFS_DIRECTORY);
            if (!new_dir) {
                terminal_writestring("Failed to create directory.\n");
            }
        } else {
            terminal_writestring("mkdir: path not found.\n");
        }
    } else if (strcmp(cmd, "cd") == 0) {
        cwd = vfs_root;
    } else if (strncmp(cmd, "cd ", 3) == 0) {
        const char* path = cmd + 3;
        struct vfs_node* dir = vfs_path_lookup(cwd, path);
        if (dir && (dir->flags & VFS_DIRECTORY)) {
            cwd = dir;
        } else {
            terminal_writestring("Directory not found: ");
            terminal_writestring(path);
            terminal_writestring("\n");
        }
    } else if (strcmp(cmd, "pwd") == 0) {
        char path_buf[256];
        get_cwd_path(path_buf, 256);
        terminal_writestring(path_buf);
        terminal_writestring("\n");
    } else if (strcmp(cmd, "meminfo") == 0) {
        pmm_info_t info;
        pmm_get_info(&info);
        terminal_writestring("Physical Memory:\n");
        terminal_writestring("  Total: ");
        terminal_writedec(info.total_pages * 4);
        terminal_writestring(" KB\n");
        terminal_writestring("  Used:  ");
        terminal_writedec(info.used_pages * 4);
        terminal_writestring(" KB\n");
        terminal_writestring("  Free:  ");
        terminal_writedec(info.free_pages * 4);
        terminal_writestring(" KB\n");
    } else if (strcmp(cmd, "heapinfo") == 0) {
        heap_info_t info;
        heap_get_info(&info);
        terminal_writestring("Kernel Heap:\n");
        terminal_writestring("  Total: ");
        terminal_writedec(info.total_bytes);
        terminal_writestring(" bytes\n");
        terminal_writestring("  Used:  ");
        terminal_writedec(info.used_bytes);
        terminal_writestring(" bytes\n");
        terminal_writestring("  Free:  ");
        terminal_writedec(info.free_bytes);
        terminal_writestring(" bytes\n");
    } else if (strcmp(cmd, "vbeinfo") == 0) {
        struct multiboot2_info *mbi = (struct multiboot2_info *)g_mb2_info_addr;
        struct multiboot2_tag_vbe *vbe_tag = find_vbe_tag(mbi);
        if (vbe_tag) {
            vbe_mode_info_t* mode_info = (vbe_mode_info_t*)vbe_tag->vbe_mode_info;
            terminal_writestring("VBE Mode: ");
            terminal_writehex(vbe_tag->vbe_mode);
            terminal_writestring("\n");
            terminal_writestring("  Resolution: ");
            terminal_writedec(mode_info->x_resolution);
            terminal_writestring("x");
            terminal_writedec(mode_info->y_resolution);
            terminal_writestring("x");
            terminal_writedec(mode_info->bits_per_pixel);
            terminal_writestring("\n");
            terminal_writestring("  PhysBasePtr: ");
            terminal_writehex(mode_info->phys_base_ptr);
            terminal_writestring("\n");
        } else {
            terminal_writestring("VBE info not found.\n");
        }
    } else if (strcmp(cmd, "info_removed") == 0) {
        const char* art[] = {
"                                           ,----,                                  ,--,                             ",
"                               ,--.      ,/   .`|                 ,--.          ,---.'|       ,----..               ",
"  .--.--.       ,---,.       ,--.'|    ,`   .'  :   ,---,       ,--.'|    ,---,.|   | :      /   /   \\   .--.--.    ",
" /  /    '.   ,'  .' |   ,--,:  : |  ;    ;     /,`--.' |   ,--,:  : |  ,'  .' |:   : |     /   .     : /  /    '.  ",
"|  :  /`. / ,---.'   |,`--.'`|  ' :.'___,/    ,' |   :  :,`--.'`|  ' :,---.'   ||   ' :    .   /   ;.  \\  :  /`. /  ",
";  |  |--`  |   |   .'|   :  :  | ||    :     |  :   |  '|   :  :  | ||   |   .';   ; '   .   ;   /  ` ;  |  |--`   ",
"|  :  ;_    :   :  |-,:   |   \\ | :;    |.';  ;  |   :  |:   |   \\ | ::   :  |-,'   | |__ ;   |  ; \\ ; |  :  ;_     ",
" \\  \\    `. :   |  ;/||   : '  '; |`----'  |  |  '   '  ;|   : '  '; |:   |  ;/||   | :.'||   :  | ; | '\\  \\    `.  ",
"  `----.   \\|   :   .''   ' ;.    ;    '   :  ;  |   |  |'   ' ;.    ;|   :   .''   :    ;.   |  ' ' ' : `----.   \\ ",
"  __ \\  \\  ||   |  |-,|   | | \\   |    |   |  '  '   :  ;|   | | \\   ||   |  |-,|   |  ./ '   ;  \\; /  | __ \\  \\  | ",
" /  /`--'  /'   :  ;/|'   : |  ; .'    '   :  |  |   |  ''   : |  ; .''   :  ;/|;   : ;    \\   \\  ',  / /  /`--'  / ",
"'--'.     / |   |    \\|   | '`--'      ;   |.'   '   :  ||   | '`--'  |   |    \\|   ,/      ;   :    / '--'.     /  ",
"  `--'---'  |   :   .''   : |          '---'     ;   |.' '   : |      |   :   .''---'        \\   \\ .'    `--'---'   ",
"            |   | ,'  ;   |.'                    '---'   ;   |.'      |   | ,'                `---`                 ",
"            `----'    '---'                              '---'        `----'                                        ",
NULL};
        for (int i = 0; art[i]; ++i) {
            terminal_writestring(art[i]);
            terminal_writestring("\n");
        }
    } else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(cmd);
        terminal_writestring("\n");
    }
after_cmd:
    
    if (strlen(cmd) > 0) {
        strcpy(command_history[history_index], cmd);
        history_index = (history_index + 1) % HISTORY_SIZE;
        if (history_count < HISTORY_SIZE) {
            history_count++;
        }
    }
    current_history_view = -1;

    shell_prompt();
}

void shell_input_char(char c) {
    // Enhanced shell input with cursor navigation (arrow keys)
    if (c == '\n') {
        terminal_putchar('\n');
        shell_buffer[shell_buffer_len] = 0;
        shell_handle_command(shell_buffer);
        shell_buffer_len = 0;
        shell_cursor_pos = 0;
        shell_buffer[0] = 0;
    } else if (c == KEY_LEFT) {
        if (shell_cursor_pos > 0) {
            erase_cursor();
            terminal_column--;
            draw_cursor();
            shell_cursor_pos--;
        }
        // Cancel selection when moving without shift
        shell_sel_anchor = (size_t)-1;
    } else if (c == KEY_RIGHT) {
        if (shell_cursor_pos < shell_buffer_len) {
            erase_cursor();
            terminal_column++;
            draw_cursor();
            shell_cursor_pos++;
        }
        shell_sel_anchor = (size_t)-1;
    } else if (c == KEY_SEL_LEFT) {
        if (shell_sel_anchor == (size_t)-1) shell_sel_anchor = shell_cursor_pos;
        if (shell_cursor_pos > 0) {
            shell_cursor_pos--;
            erase_cursor(); terminal_column--; draw_cursor();
            shell_redraw_line_with_selection();
        }
    } else if (c == KEY_SEL_RIGHT) {
        if (shell_sel_anchor == (size_t)-1) shell_sel_anchor = shell_cursor_pos;
        if (shell_cursor_pos < shell_buffer_len) {
            shell_cursor_pos++;
            erase_cursor(); terminal_column++; draw_cursor();
            shell_redraw_line_with_selection();
        }
    } else if (c == '\b') {
        if (shell_cursor_pos > 0) {
            // Move cursor left
            erase_cursor();
            terminal_column--;
            draw_cursor();
            // Remove the character before the cursor position
            memmove(shell_buffer + shell_cursor_pos - 1, shell_buffer + shell_cursor_pos, shell_buffer_len - shell_cursor_pos + 1);
            shell_cursor_pos--;
            shell_buffer_len--;
            // Redraw the remainder of the line
            for (size_t i = shell_cursor_pos; i < shell_buffer_len; i++) {
                terminal_putchar(shell_buffer[i]);
            }
            // Erase the leftover last character visually
            terminal_putchar(' ');
            // Move cursor back to correct position
            size_t move = shell_buffer_len - shell_cursor_pos + 1;
            for (size_t i = 0; i < move; i++) {
                erase_cursor();
                terminal_column--;
                draw_cursor();
            }
        }
        shell_sel_anchor = (size_t)-1;
    } else if (c == '\t') {
        // Autocomplete current buffer from SHELL_COMMANDS
        if (shell_buffer_len > 0) {
            const char* best = NULL;
            size_t best_len = 0;
            for (size_t i = 0; i < NUM_SHELL_COMMANDS; ++i) {
                const char* cmd = SHELL_COMMANDS[i];
                size_t j = 0;
                while (j < shell_buffer_len && cmd[j] && cmd[j] == shell_buffer[j]) {
                    j++;
                }
                if (j == shell_buffer_len) {
                    // Candidate match; choose the shortest unique completion that extends input
                    if (!best || strlen(cmd) < best_len) {
                        best = cmd;
                        best_len = strlen(cmd);
                    }
                }
            }
            if (best && best_len > shell_buffer_len) {
                // Replace current line content visually: erase current, write completion
                size_t erase = shell_buffer_len;
                for (size_t i = 0; i < erase; i++) {
                    terminal_putchar('\b');
                }
                strcpy(shell_buffer, best);
                shell_buffer_len = strlen(shell_buffer);
                shell_cursor_pos = shell_buffer_len;
                terminal_writestring(shell_buffer);
            }
        }
    } else if (c >= 32 && c < 127) {
        if (shell_buffer_len < SHELL_BUFFER_SIZE - 1) {
            // Insert character at cursor position (clear selection if any)
            if (shell_sel_anchor != (size_t)-1 && shell_sel_anchor != shell_cursor_pos) {
                size_t sel_start = (shell_sel_anchor < shell_cursor_pos) ? shell_sel_anchor : shell_cursor_pos;
                size_t sel_end = (shell_sel_anchor < shell_cursor_pos) ? shell_cursor_pos : shell_sel_anchor;
                memmove(shell_buffer + sel_start, shell_buffer + sel_end, shell_buffer_len - sel_end + 1);
                shell_buffer_len -= (sel_end - sel_start);
                shell_cursor_pos = sel_start;
                shell_sel_anchor = (size_t)-1;
                shell_redraw_line_with_selection();
            }
            // Insert
            memmove(shell_buffer + shell_cursor_pos + 1, shell_buffer + shell_cursor_pos, shell_buffer_len - shell_cursor_pos + 1);
            shell_buffer[shell_cursor_pos] = c;
            shell_buffer_len++;
            // Print the rest of the line from the insertion point
            for (size_t i = shell_cursor_pos; i < shell_buffer_len; i++) {
                terminal_putchar(shell_buffer[i]);
            }
            shell_cursor_pos++;
            // Move cursor back to after inserted character
            size_t move = shell_buffer_len - shell_cursor_pos;
            for (size_t i = 0; i < move; i++) {
                erase_cursor();
                terminal_column--;
                draw_cursor();
            }
        }
    } else if (c == KEY_UP || c == KEY_DOWN) {
        if (history_count == 0) return;

        if (c == KEY_UP) {
            if (current_history_view == -1) {
                current_history_view = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;
            } else {
                int oldest_entry = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
                if (current_history_view != oldest_entry) {
                    current_history_view = (current_history_view - 1 + HISTORY_SIZE) % HISTORY_SIZE;
                }
            }
        } else { // KEY_DOWN
            if (current_history_view != -1) {
                if (current_history_view == (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE) {
                    current_history_view = -1; // Exit history view
                } else {
                    current_history_view = (current_history_view + 1) % HISTORY_SIZE;
                }
            }
        }

        // Erase current line
        for (size_t i = 0; i < shell_buffer_len; i++) {
            terminal_putchar('\b');
        }

        if (current_history_view != -1) {
            strcpy(shell_buffer, command_history[current_history_view]);
            shell_buffer_len = strlen(shell_buffer);
            terminal_writestring(shell_buffer);
        } else {
            shell_buffer_len = 0;
            shell_buffer[0] = '\0';
        }
        shell_cursor_pos = shell_buffer_len;
        shell_sel_anchor = (size_t)-1;
    } else if (c == KEY_COPY) {
        // Copy selection to clipboard
        if (shell_sel_anchor != (size_t)-1 && shell_sel_anchor != shell_cursor_pos) {
            size_t sel_start = (shell_sel_anchor < shell_cursor_pos) ? shell_sel_anchor : shell_cursor_pos;
            size_t sel_end   = (shell_sel_anchor < shell_cursor_pos) ? shell_cursor_pos : shell_sel_anchor;
            size_t n = sel_end - sel_start;
            if (n >= SHELL_BUFFER_SIZE) n = SHELL_BUFFER_SIZE - 1;
            memcpy(clipboard, shell_buffer + sel_start, n);
            clipboard[n] = '\0';
        }
    } else if (c == KEY_PASTE) {
        // Paste clipboard at cursor (replace selection if any)
        size_t clip_len = strlen(clipboard);
        if (clip_len > 0) {
            if (shell_sel_anchor != (size_t)-1 && shell_sel_anchor != shell_cursor_pos) {
                size_t sel_start = (shell_sel_anchor < shell_cursor_pos) ? shell_sel_anchor : shell_cursor_pos;
                size_t sel_end = (shell_sel_anchor < shell_cursor_pos) ? shell_cursor_pos : shell_sel_anchor;
                memmove(shell_buffer + sel_start, shell_buffer + sel_end, shell_buffer_len - sel_end + 1);
                shell_buffer_len -= (sel_end - sel_start);
                shell_cursor_pos = sel_start;
                shell_sel_anchor = (size_t)-1;
            }
            if (shell_buffer_len + clip_len >= SHELL_BUFFER_SIZE) clip_len = SHELL_BUFFER_SIZE - 1 - shell_buffer_len;
            memmove(shell_buffer + shell_cursor_pos + clip_len, shell_buffer + shell_cursor_pos, shell_buffer_len - shell_cursor_pos + 1);
            memcpy(shell_buffer + shell_cursor_pos, clipboard, clip_len);
            shell_buffer_len += clip_len;
            // Redraw from cursor
            for (size_t i = shell_cursor_pos; i < shell_buffer_len; i++) terminal_putchar(shell_buffer[i]);
            // Move cursor back to end of pasted text
            size_t move = shell_buffer_len - (shell_cursor_pos + clip_len);
            for (size_t i = 0; i < move; i++) { erase_cursor(); terminal_column--; draw_cursor(); }
            shell_cursor_pos += clip_len;
        }
    }
}

void terminal_writehex(uint64_t n) {
    char buffer[17];
    char* hex_chars = "0123456789abcdef";
    buffer[16] = '\0';
    int i = 15;
    if (n == 0) {
        terminal_writestring("0x0");
        return;
    }
    terminal_writestring("0x");
    while (n > 0 && i >= 0) {
        buffer[i--] = hex_chars[n % 16];
        n /= 16;
    }
    terminal_writestring(&buffer[i + 1]);
}

void terminal_writedec(size_t n) {
    if (n == 0) {
        terminal_putchar('0');
        return;
    }

    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }

    for (int j = i - 1; j >= 0; j--) {
        terminal_putchar(buf[j]);
    }
}

void delay(int milliseconds) {
    long i = milliseconds * 500000; // This is a very rough busy-wait loop.
    while (i-- > 0) {
        __asm__ __volatile__("nop");
    }
}

static struct framebuffer_info fb_info;
static bool graphics_initialized = false;

void draw_progress_bar_background() {
    siv_draw_rect(fb_info.width / 2 - 202, fb_info.height / 2 - 12, 404, 24, 0x00666666, true);
    siv_draw_rect(fb_info.width / 2 - 200, fb_info.height / 2 - 10, 400, 20, 0x00333333, true);
}

void update_progress_bar(int percentage, const char* text) {
    if (!graphics_initialized) return;

    int bar_width = 400 * percentage / 100;
    siv_draw_rect(fb_info.width / 2 - 200, fb_info.height / 2 - 10, bar_width, 20, 0x0000AA00, true);

    // Clear previous text by drawing a rectangle over it
    siv_draw_rect(fb_info.width / 2 - 200, fb_info.height / 2 + 15, 400, 20, 0x00112233, true);
    siv_draw_text(fb_info.width / 2 - 200, fb_info.height / 2 + 25, text, 1.0f, 0xFFFFFFFF);
}

void init_graphics(struct multiboot2_tag_framebuffer* fb_tag) {
    if (fb_tag->framebuffer_type == 2) {
        // Text framebuffer only; leave graphics disabled
        serial_writestring("Bootloader provided EGA text framebuffer. Graphics disabled.\n");
        graphics_initialized = false;
        return;
    }

    // Ensure framebuffer physical memory is identity-mapped before use
    size_t fb_bytes = (size_t)fb_tag->framebuffer_pitch * fb_tag->framebuffer_height;
    if (!vmm_identity_map_range((uint64_t)fb_tag->framebuffer_addr, fb_bytes, PAGE_PRESENT | PAGE_WRITABLE)) {
        serial_writestring("VMM: Failed to map framebuffer. Graphics disabled.\n");
        graphics_initialized = false;
        return;
    }

    siv_init(fb_tag->framebuffer_width, fb_tag->framebuffer_height, fb_tag->framebuffer_pitch, fb_tag->framebuffer_bpp, (void*)(uintptr_t)fb_tag->framebuffer_addr);
    fb_info.width = fb_tag->framebuffer_width;
    fb_info.height = fb_tag->framebuffer_height;
    siv_init_font();
    siv_clear(0x00112233);
    graphics_initialized = true;
}

// (removed unused is_graphics_available and render_info_banner_siv)

void init_terminal() {
    terminal_initialize();
    serial_writestring("Welcome to SentinelOS!\n");
    shell_prompt();
}

void kernel_main(uint64_t multiboot_info_addr) {
    g_mb2_info_addr = multiboot_info_addr;
    serial_init();
    serial_writestring("Serial Initialized\n");

    struct multiboot2_info *mbi = (struct multiboot2_info *)multiboot_info_addr;
    
    struct multiboot2_tag_mmap *mmap_tag = find_mmap_tag(mbi);
    if (mmap_tag) {
        if (!pmm_init(mmap_tag)) {
            serial_writestring("PMM initialization failed. Halting.\n");
            asm ("cli; hlt");
        }
    } else {
        serial_writestring("Memory map not found!\n");
        // Halt or handle error appropriately
        return;
    }

    heap_init();

    vmm_init();

    struct multiboot2_tag_framebuffer *fb_tag = find_framebuffer_tag(mbi);
    struct multiboot2_tag_vbe *vbe_tag = find_vbe_tag(mbi);

    if (vbe_tag) {
        vbe_init(vbe_tag);
    }

    if (fb_tag) {
        init_graphics(fb_tag);
        draw_progress_bar_background();
        update_progress_bar(0, "Initializing...");
        delay(500);
    }

    idt_install();
    serial_writestring("IDT loaded\n");
    update_progress_bar(20, "GDT and IDT loaded.");
    delay(500);

    isr_install();
    pic_remap();
    update_progress_bar(40, "Interrupts enabled.");
    delay(500);
    
    // Find the initrd module
    struct multiboot2_tag_module *module_tag = find_module_tag(mbi, "initrd.tar");

    if (module_tag) {
        update_progress_bar(60, "Initrd found. Initializing...");
        delay(500);
        vfs_root = initrd_init((uintptr_t)module_tag->mod_start);
        cwd = vfs_root;
        update_progress_bar(80, "Initrd initialized.");
        delay(500);
    } else {
        serial_writestring("Initrd module not found.\n");
        vfs_root = NULL;
        cwd = NULL;
        update_progress_bar(80, "Initrd not found.");
        delay(500);
    }

    keyboard_init();
    mouse_init();
    
    update_progress_bar(100, "Boot complete.");
    delay(1000);

    // Initialize terminal shell
    init_terminal();
    sti();
    serial_writestring("Keyboard and mouse initialized, terminal activated. Interrupts unmasked.\n");

    // Enter idle loop
    while (1) {
        asm("hlt");
    }
}