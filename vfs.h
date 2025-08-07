#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define VFS_FILE 0x01
#define VFS_DIRECTORY 0x02

struct vfs_node;

typedef size_t (*vfs_read_t)(struct vfs_node*, size_t, size_t, uint8_t*);
typedef size_t (*vfs_write_t)(struct vfs_node*, size_t, size_t, uint8_t*);
typedef void (*vfs_open_t)(struct vfs_node*);
typedef void (*vfs_close_t)(struct vfs_node*);
typedef struct dirent* (*vfs_readdir_t)(struct vfs_node*, uint32_t);
typedef struct vfs_node* (*vfs_finddir_t)(struct vfs_node*, char* name);
typedef struct vfs_node* (*vfs_create_t)(struct vfs_node*, char* name, uint32_t flags);
typedef int (*vfs_delete_t)(struct vfs_node*, char* name);

struct dirent {
    char name[256];
    uint32_t inode_num;
};

struct vfs_node {
    char name[256];
    uint32_t flags;
    uint32_t length;
    uint32_t inode;
    void* ptr; // Opaque pointer for filesystem-specific data

    vfs_read_t read;
    vfs_write_t write;
    vfs_open_t open;
    vfs_close_t close;
    vfs_readdir_t readdir;
    vfs_finddir_t finddir;
    vfs_create_t create;
    vfs_delete_t delete;

    struct vfs_node* parent;
    struct vfs_node* first_child;
    struct vfs_node* next_sibling;
};

extern struct vfs_node* vfs_root;

void vfs_init();
size_t vfs_read(struct vfs_node* node, size_t offset, size_t size, uint8_t* buffer);
size_t vfs_write(struct vfs_node* node, size_t offset, size_t size, uint8_t* buffer);
void vfs_open(struct vfs_node* node);
void vfs_close(struct vfs_node* node);
struct dirent* vfs_readdir(struct vfs_node* node, uint32_t index);
struct vfs_node* vfs_finddir(struct vfs_node* node, char* name);
void vfs_mount(struct vfs_node* node);
struct vfs_node* vfs_create(struct vfs_node* parent, char* name, uint32_t flags);
int vfs_delete(struct vfs_node* parent, char* name);
struct vfs_node* vfs_path_lookup(struct vfs_node* context, const char* path);

#endif // VFS_H 