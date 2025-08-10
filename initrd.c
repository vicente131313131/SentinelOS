/* initrd.c – Parse a simple tar-based initial RAM disk into a VFS tree */
#include "initrd.h"
#include "vfs.h"
#include "string.h"
#include "serial.h"

#define MAX_FILES 64 // Increased max files
static struct vfs_node initrd_nodes[MAX_FILES];
static int n_nodes = 0;
static struct dirent dirent; // For readdir

// Forward declarations for our new functions
struct vfs_node* finddir_initrd(struct vfs_node* node, char* name);
struct dirent* readdir_initrd(struct vfs_node* node, uint32_t index);
struct vfs_node* create_initrd(struct vfs_node* parent, char* name, uint32_t flags);
int delete_initrd(struct vfs_node* parent, char* name);

// Function to get the size of a file from the tar header
/* Read octal ASCII size from tar header field. */
unsigned int get_size(const char *in) {
    unsigned int size = 0;
    unsigned int j;
    unsigned int count = 1;
    for (j = 11; j > 0; j--, count *= 8) {
        if (in[j-1] >= '0' && in[j-1] <= '7') {
            size += ((in[j - 1] - '0') * count);
        }
    }
    return size;
}

// Read function for initrd files
/* Backend read for initrd file nodes – memcpy from embedded image. */
size_t initrd_read(struct vfs_node* node, size_t offset, size_t size, uint8_t* buffer) {
    if (offset > node->length) return 0;
    if (offset + size > node->length) size = node->length - offset;
    memcpy(buffer, (uint8_t*)node->ptr + offset, size);
    return size;
}

// Find a file in a directory
/* Directory lookup among children by name. */
struct vfs_node* finddir_initrd(struct vfs_node* node, char* name) {
    if (!(node->flags & VFS_DIRECTORY)) return NULL;
    
    struct vfs_node* child = node->first_child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

// Read a directory entry
/* Readdir implementation returning one entry at a time. */
struct dirent* readdir_initrd(struct vfs_node* node, uint32_t index) {
    if (!(node->flags & VFS_DIRECTORY)) return NULL;

    struct vfs_node* child = node->first_child;
    for (uint32_t i = 0; i < index && child; i++) {
        child = child->next_sibling;
    }

    if (child) {
        strcpy(dirent.name, child->name);
        dirent.inode_num = child->inode;
        return &dirent;
    }
    
    return NULL;
}

/* Create a child node under parent (file or directory). */
struct vfs_node* create_initrd(struct vfs_node* parent, char* name, uint32_t flags) {
    if (!(parent->flags & VFS_DIRECTORY)) return NULL;
    if (n_nodes >= MAX_FILES) return NULL;
    if (finddir_initrd(parent, name)) return NULL; // Exists

    struct vfs_node* new_node = &initrd_nodes[n_nodes++];
    strcpy(new_node->name, name);
    new_node->flags = flags;
    new_node->length = 0;
    new_node->ptr = NULL;
    new_node->parent = parent;
    new_node->first_child = NULL;
    new_node->next_sibling = parent->first_child;
    parent->first_child = new_node;

    if (flags & VFS_FILE) {
        new_node->read = initrd_read;
    } else {
        new_node->readdir = readdir_initrd;
        new_node->finddir = finddir_initrd;
        new_node->create = create_initrd;
        new_node->delete = delete_initrd;
    }

    return new_node;
}

/* Remove a child node by name (simple unlink; memory not reclaimed). */
int delete_initrd(struct vfs_node* parent, char* name) {
    if (!(parent->flags & VFS_DIRECTORY)) return -1;

    struct vfs_node* current = parent->first_child;
    struct vfs_node* prev = NULL;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            if (prev) {
                prev->next_sibling = current->next_sibling;
            } else {
                parent->first_child = current->next_sibling;
            }
            // Memory leak, but ok for now
            current->name[0] = '\0'; 
            return 0; // Success
        }
        prev = current;
        current = current->next_sibling;
    }
    return -1; // Not found
}

// Helper to get/create a node for a given path
/* Ensure the directory path exists under root; return the deepest node. */
static struct vfs_node* get_or_create_node_from_path(struct vfs_node* root, const char* path) {
    char* p = (char*)path;
    char* q;
    struct vfs_node* current_node = root;

    // ignore leading './'
    if (p[0] == '.' && p[1] == '/') {
        p += 2;
    }
    
    while ((q = strchr(p, '/'))) {
        if (q > p) {
            char dirname[256];
            memcpy(dirname, p, q - p);
            dirname[q - p] = '\0';
            
            struct vfs_node* found = finddir_initrd(current_node, dirname);
            if (found) {
                current_node = found;
            } else {
                current_node = create_initrd(current_node, dirname, VFS_DIRECTORY);
            }
        }
        p = q + 1;
    }
    return current_node;
}

// Initialize the initrd and build the VFS tree
/* Build the VFS tree by scanning the tar archive at `location`. */
struct vfs_node* initrd_init(uintptr_t location) {
    uintptr_t current_location = location;
    n_nodes = 0; // Reset node count
    serial_writestring("Initializing initrd...\n");

    // Create a root node for the initrd filesystem
    struct vfs_node* root = &initrd_nodes[n_nodes++];
    strcpy(root->name, "/");
    root->flags = VFS_DIRECTORY;
    root->length = 0;
    root->ptr = NULL;
    root->parent = NULL;
    root->first_child = NULL;
    root->next_sibling = NULL;
    root->finddir = finddir_initrd;
    root->readdir = readdir_initrd;
    root->create = create_initrd;
    root->delete = delete_initrd;

    while (*(char*)current_location && n_nodes < MAX_FILES) {
        struct tar_header *header = (struct tar_header *)current_location;
        int size = get_size(header->size);

        // Filter out macOS AppleDouble and PAX extended headers
        if (strncmp(header->name, "._", 2) != 0 && strncmp(header->name, "PaxHeader", 9) != 0 && header->name[0] != '\0') {
            
            // Get parent directory
            struct vfs_node* parent = get_or_create_node_from_path(root, header->name);
            char* basename = strrchr(header->name, '/');
            if (basename) {
                basename++; // move past '/'
            } else {
                basename = header->name;
            }

            if (strlen(basename) > 0) {
                 if (header->typeflag == '5') { // Directory
                    if (!finddir_initrd(parent, basename)) {
                        create_initrd(parent, basename, VFS_DIRECTORY);
                    }
                } else { // File
                    // Skip AppleDouble resource forks explicitly
                    if (strncmp(basename, "._", 2) == 0) {
                        // skip
                    } else {
                    struct vfs_node* new_node = create_initrd(parent, basename, VFS_FILE);
                    if (new_node) {
                        new_node->length = size;
                        new_node->ptr = (void*)(current_location + 512);
                    }
                    }
                }
            }
        }

        current_location += 512; // Move past header
        if (size > 0) {
            current_location += (size + 511) & ~511;
        }
    }

    serial_writestring("Initrd initialization complete.\n");
    return root;
} 