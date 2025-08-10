#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>
#include "vfs.h"

// Struct for the tar header
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
};

// Initializes the initrd and returns the root VFS node
struct vfs_node* initrd_init(uintptr_t location);

// Expose basic directory operations used by the simple initrd-backed memfs
struct vfs_node* finddir_initrd(struct vfs_node* node, char* name);
struct dirent* readdir_initrd(struct vfs_node* node, uint32_t index);
struct vfs_node* create_initrd(struct vfs_node* parent, char* name, uint32_t flags);
int delete_initrd(struct vfs_node* parent, char* name);

#endif // INITRD_H 