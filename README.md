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
  * `help`, `clear`, `info`, `sent`, `ls`, `cat`, `mkdir`, `touch`, `rm`, `cd`, `pwd`, `meminfo`, `heapinfo`, `vbeinfo`.
* PS/2 keyboard and mouse drivers.

Upcoming milestones include an ELF user-space loader, AHCI/NVMe driver, pre-emptive multitasking and a minimal TCP/IP stack.

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
$ qemu-system-x86_64 \
    -cdrom sentinelos.iso \
    -m 256M \
    -serial stdio \
    -vga std
```

Parameter summary

| Flag | Purpose |
|------|---------|
| `-cdrom` | Attach generated ISO file |
| `-m` | Allocate guest memory (256 MiB is sufficient) |
| `-serial stdio` | Redirect COM1 to the host terminal |
| `-vga std` | 32-bit colour framebuffer compatible with SpringIntoView |

For physical hardware the ISO is El-Torito/MBR hybrid; it can be flashed with standard imaging utilities (Balena Etcher, `dd`, Rufus). **Note:** Real-hardware support is experimental.

---

## 5. Shell Command Reference

| Command | Description |
|---------|-------------|
| `help` | List available commands |
| `clear` | Clear screen |
| `info` | Display project banner and version information |
| `sent` | Demonstrate terminal colour palette |
| `ls [path]` | List directory contents |
| `cat <file>` | Show file contents |
| `mkdir <dir>` | Create directory |
| `touch <file>` | Create empty file |
| `rm <file>` | Delete file |
| `cd <dir>` / `pwd` | Navigate virtual file system |
| `meminfo` / `heapinfo` | Memory statistics |
| `vbeinfo` | VESA framebuffer mode information |

---

## 6. Repository Layout

```
.
├─ boot/             # Assembly routines (boot sector & kernel entry)
├─ drivers/          # Device drivers (keyboard, mouse, PIC, etc.)
├─ SpringIntoView/   # Lightweight framebuffer graphics library
├─ fs/               # Files copied into the initrd
├─ kernel.c          # Kernel entry point and main loop
├─ pmm.c, heap.c     # Memory management implementation
├─ vfs.c, initrd.c   # Virtual File System and initrd loader
├─ Makefile          # Top-level build script
└─ linker.ld         # Linker script
```

---

## 7. Contribution Guidelines

Contributions that improve the code base, add new drivers or enhance documentation are welcome.

1. Fork the repository and create a feature branch.
2. Follow the existing coding style; keep commits focused.
3. Verify the build with `make` and boot the resulting image in QEMU before opening a pull request.

For larger changes please open an issue to discuss design details prior to implementation.

---

## 8. License

SentinelOS is distributed under the **MIT License**.  See `LICENSE` for the full text.

---