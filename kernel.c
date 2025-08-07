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
    for (struct multiboot2_tag *tag = (struct multiboot2_tag *)((uint8_t*)mbi + 8);
         tag->type != MULTIBOOT2_TAG_TYPE_END;
         tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
            struct multiboot2_tag_module *mod = (struct multiboot2_tag_module *)tag;
            if (strcmp(mod->cmdline, name) == 0) {
                return mod;
            }
        }
    }
    return NULL;
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
static char command_history[HISTORY_SIZE][SHELL_BUFFER_SIZE];
static int history_count = 0;
static int history_index = 0;
static int current_history_view = -1;

static struct vfs_node* cwd = NULL;

// Global variable to hold the Multiboot 2 info address
static uint64_t g_mb2_info_addr = 0;

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

// Function to write a null-terminated string
void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
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
        terminal_writestring(" - sent: Display beautiful ASCII art\n");
        terminal_writestring(" - vbeinfo: Show VBE info\n");
    } else if (strcmp(cmd, "clear") == 0) {
        shell_clear();
    } else if (strcmp(cmd, "sent") == 0) {
        terminal_writestring(" ____  _____ _   _ _____ ___ _   _ _____ _     \n");
        terminal_writestring("/ ___|| ____| \\ | |_   _|_ _| \\ | | ____| |    \n");
        terminal_writestring("\\___ \\|  _| |  \\| | | |  | ||  \\| |  _| | |    \n");
        terminal_writestring(" ___) | |___| |\\  | | |  | || |\\  | |___| |___ \n");
        terminal_writestring("|____/|_____|_| \\_| |_| |___|_| \\_|_____|_____|\n");
        terminal_writestring("\n");
        uint8_t original_color = terminal_color;
        for (uint8_t i = 0; i < 16; i++) {
            terminal_set_color(VGA_LIGHT_GREY | (i << 4));
            terminal_writestring("  ");
        }
        terminal_writestring("\n");
        for (uint8_t i = 0; i < 16; i++) {
            terminal_set_color(i | (VGA_BLACK << 4));
            terminal_writestring("Aa");
        }
        terminal_set_color(original_color);
        terminal_writestring("\n");
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        terminal_writestring(cmd + 5);
        terminal_writestring("\n");
    } else if (strcmp(cmd, "ls") == 0 || strncmp(cmd, "ls ", 3) == 0) {
        const char* path = (strcmp(cmd, "ls") == 0) ? "" : cmd + 3;
        struct vfs_node* node_to_ls = (*path) ? vfs_path_lookup(cwd, path) : cwd;
        
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
        
        if (parent_node) {
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
        
        if (parent_node) {
            if (vfs_delete(parent_node, basename) != 0) {
                terminal_writestring("Failed to delete file.\n");
            }
        } else {
            terminal_writestring("rm: path not found.\n");
        }
    } else if (strcmp(cmd, "info") == 0) {
        const char* art_info[] = {
" ▗▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖▗▖    ▗▄▖  ▗▄▄▖",
"▐▌   ▐▌   ▐▛▚▖▐▌  █    █  ▐▛▚▖▐▌▐▌   ▐▌   ▐▌ ▐▌▐▌   ",
" ▝▀▚▖▐▛▀▀▘▐▌ ▝▜▌  █    █  ▐▌ ▝▜▌▐▛▀▀▘▐▌   ▐▌ ▐▌ ▝▀▚▖",
"▗▄▄▞▘▐▙▄▄▖▐▌  ▐▌  █  ▗▄█▄▖▐▌  ▐▌▐▙▄▄▖▐▙▄▄▖▝▚▄▞▘▗▄▄▞▘",
"                                                    ",
"                                                    ",
"                                                    ",
NULL};
        for(int i=0; art_info[i]; ++i){
            terminal_writestring(art_info[i]);
            terminal_writestring("\n");
        }
        terminal_writestring("SentinelOS v0.2 Pre-Alpha by Vicente Velasquez, Last updated: 2025 June 12th\n");
    } else if (strcmp(cmd, "graphics") == 0) {
        terminal_writestring("Graphics mode is only available at boot.\n");
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
        
        if (parent_node) {
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
    } else if (c == KEY_RIGHT) {
        if (shell_cursor_pos < shell_buffer_len) {
            erase_cursor();
            terminal_column++;
            draw_cursor();
            shell_cursor_pos++;
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
    } else if (c >= 32 && c < 127) {
        if (shell_buffer_len < SHELL_BUFFER_SIZE - 1) {
            // Insert character at cursor position
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