# SentinelOS

SentinelOS is a 64-bit hobby project operating system. The project is implemented in C (cross-compiled with **x86_64-elf-gcc**) and x86-64 assembly and targets virtual machines such as QEMU, it is not recommended to run this in real hardware, unless you know what you are doing

---

## 1. Feature Overview

* 64-bit long-mode kernel initialised by a Multiboot-2 compliant GRUB bootloader.
* Graphical boot splash rendered with the in-tree *SpringIntoView* immediate-mode framebuffer library.
* Full interrupt infrastructure (IDT, custom ISRs, PIC remap).
* Serial logging on COM1 for non-intrusive debugging (`-serial stdio`).
* Bitmap-based Physical Memory Manager (PMM) and free-list Kernel Heap allocator.
* Virtual File System (VFS) backed by an **initrd** (`initrd.tar`).
* Shell with inline editing & command history supporting:
* `help`, `clear`, `info`, `ls`, `cat`, `mkdir`, `touch`, `rm`, `cd`, `pwd`, `meminfo`, `heapinfo`, `vbeinfo`.
* PS/2 keyboard and mouse drivers.


---

## 2. System Requirements

| Component | Version / Remark |
|-----------|------------------|
| **Compiler** | `x86_64-elf-gcc` 10.x or newer |
| **Assembler** | `nasm` |
| **Build tools** | `make`, `xorriso`, `grub-mkrescue` |
| **Emulator** | `qemu-system-x86_64` (recommended) |

A pre-built cross-compiler is available via **Homebrew** (macOS) or most Linux distributions. Detailed instructions can be found in `docs/toolchain.md`.

---

## 3. Building the Project

```bash
# Clone repository
$ git clone https://github.com/your-account/SentinelOS.git
$ cd SentinelOS

# Produce bootloader, kernel and ISO image
$ make
```

The build generates `sentinelos.iso` in the project root.

---

## 4. Running under QEMU

```bash
$ qemu-system-x86_64 -cdrom sentinelos.iso -m 256M -serial stdio -vga std
```

Parameter summary

| Flag | Purpose |
|------|---------|
| `-cdrom` | Attach generated ISO file |
| `-m` | Allocate guest memory (256 MiB is sufficient) |
| `-serial stdio` | Redirect COM1 to the host terminal |
| `-vga std` | 32-bit colour framebuffer compatible with SpringIntoView |

---

## 5. Command Reference

| Command | Description |
|---------|-------------|
| `help` | List available commands |
| `clear` | Clear screen |
| `info` | Display project and version information |
| `ls [path]` | List directory contents |
| `cat <file>` | Show file contents |
| `mkdir <dir>` | Create directory |
| `touch <file>` | Create empty file |
| `rm <file>` | Delete file |
| `cd <dir>` / `pwd` | Navigate virtual file system |
| `meminfo` / `heapinfo` | Memory statistics |
| `vbeinfo` | VESA framebuffer mode information |

---

## 6. Libraries

- **stb_truetype.h** - Public domain font rendering library

## 7. License

This project is licensed under the MIT License. See the `LICENSE` file for details.
