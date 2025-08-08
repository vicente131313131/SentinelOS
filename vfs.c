/* vfs.c – Simple Virtual Filesystem façade dispatching to node callbacks */
#include "vfs.h"
#include "string.h"

struct vfs_node* vfs_root = 0;

/* Reset VFS root. */
void vfs_init() {
    vfs_root = 0;
}

/* Set the root node. */
void vfs_mount(struct vfs_node* node) {
    vfs_root = node;
}


/* Dispatch to filesystem-specific read if present. */
size_t vfs_read(struct vfs_node* node, size_t offset, size_t size, uint8_t* buffer) {
    if (node->read != 0)
        return node->read(node, offset, size, buffer);
    else
        return 0;
}

/* Dispatch write. */
size_t vfs_write(struct vfs_node* node, size_t offset, size_t size, uint8_t* buffer) {
    if (node->write != 0)
        return node->write(node, offset, size, buffer);
    else
        return 0;
}

/* Dispatch open. */
void vfs_open(struct vfs_node* node) {
    if (node->open != 0)
        return node->open(node);
}

/* Dispatch close. */
void vfs_close(struct vfs_node* node) {
    if (node->close != 0)
        return node->close(node);
}

/* Dispatch readdir for directory nodes. */
struct dirent* vfs_readdir(struct vfs_node* node, uint32_t index) {
    if ((node->flags & VFS_DIRECTORY) && node->readdir != 0)
        return node->readdir(node, index);
    else
        return 0;
}

/* Dispatch finddir for directory nodes. */
struct vfs_node* vfs_finddir(struct vfs_node* node, char* name) {
    if ((node->flags & VFS_DIRECTORY) && node->finddir != 0)
        return node->finddir(node, name);
    else
        return 0;
}

/* Dispatch create under a directory. */
struct vfs_node* vfs_create(struct vfs_node* parent, char* name, uint32_t flags) {
    if (parent->create != 0)
        return parent->create(parent, name, flags);
    else
        return 0;
}

/* Dispatch delete under a directory. */
int vfs_delete(struct vfs_node* parent, char* name) {
    if (parent->delete != 0)
        return parent->delete(parent, name);
    else
        return 0;
}

/* Resolve an absolute or relative path from context, handling '.' and '..'. */
struct vfs_node* vfs_path_lookup(struct vfs_node* context, const char* path) {
    if (!path || path[0] == '\0') {
        return context;
    }

    struct vfs_node* current_node;
    if (path[0] == '/') {
        current_node = vfs_root;
        path++;
    } else {
        current_node = context;
    }

    char name_part[256];
    const char* p = path;

    while (*p) {
        const char* q = strchr(p, '/');
        size_t len;
        if (q) {
            len = q - p;
            p = q + 1;
        } else {
            len = strlen(p);
            p += len;
        }

        if (len == 0) continue;

        memcpy(name_part, p - (q ? (len + 1) : len), len);
        name_part[len] = '\0';
        
        // Handle . and ..
        if (strcmp(name_part, ".") == 0) {
            continue;
        }
        if (strcmp(name_part, "..") == 0) {
            if (current_node->parent) {
                current_node = current_node->parent;
            }
            continue;
        }

        current_node = vfs_finddir(current_node, name_part);
        if (!current_node) {
            return NULL; // Not found
        }
    }
    
    return current_node;
} 