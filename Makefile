# Compiler and assembler settings
ASM = nasm
CC = x86_64-elf-gcc
LD = x86_64-elf-ld

# Flags
ASMFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-stack-check -fno-pie -fno-pic -mcmodel=large -mno-red-zone -c
LDFLAGS = -T linker.ld -z nodefaultlib -z noexecstack

# Files
BOOT = boot.bin
KERNEL = kernel.bin
ISO = sentinelos.iso
SPRINGINTOVIEW = SpringIntoView/spring_into_view.o
INITRD = initrd.tar
INITRD_SRC = fs/

# Build targets
all: $(ISO)

$(INITRD): $(wildcard $(INITRD_SRC)/*)
	COPYFILE_DISABLE=1 tar --format=ustar --no-mac-metadata --exclude 'PaxHeader*' --exclude '._*' -cf $(INITRD) -C $(INITRD_SRC) .

$(BOOT): boot.asm
	$(ASM) -f bin boot.asm -o $(BOOT)

multiboot_header.o: multiboot_header.asm
	$(ASM) $(ASMFLAGS) multiboot_header.asm -o multiboot_header.o

kernel_entry.o: kernel_entry.asm
	$(ASM) $(ASMFLAGS) kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) kernel.c -o kernel.o

isr.o: isr.c isr.h
	$(CC) $(CFLAGS) isr.c -o isr.o

idt.o: idt.c idt.h
	$(CC) $(CFLAGS) idt.c -o idt.o

pic.o: pic.c pic.h
	$(CC) $(CFLAGS) pic.c -o pic.o

isr_asm.o: isr.asm
	$(ASM) $(ASMFLAGS) isr.asm -o isr_asm.o

pmm.o: pmm.c pmm.h
	$(CC) $(CFLAGS) pmm.c -o pmm.o

pit.o: pit.c pit.h
	$(CC) $(CFLAGS) pit.c -o pit.o

keyboard.o: keyboard.c keyboard.h
	$(CC) $(CFLAGS) keyboard.c -o keyboard.o

serial.o: serial.c serial.h
	$(CC) $(CFLAGS) serial.c -o serial.o

string.o: string.c string.h
	$(CC) $(CFLAGS) string.c -o string.o

vfs.o: vfs.c vfs.h
	$(CC) $(CFLAGS) vfs.c -o vfs.o

initrd.o: initrd.c initrd.h vfs.h
	$(CC) $(CFLAGS) initrd.c -o initrd.o

heap.o: heap.c
	$(CC) $(CFLAGS) heap.c -o heap.o

vmm.o: vmm.c vmm.h mem.h
	$(CC) $(CFLAGS) vmm.c -o vmm.o

mouse.o: mouse.c mouse.h
	$(CC) $(CFLAGS) mouse.c -o mouse.o


vbe.o: vbe.c vbe.h
	$(CC) $(CFLAGS) vbe.c -o vbe.o

SpringIntoView/spring_into_view.o: SpringIntoView/spring_into_view.c SpringIntoView/spring_into_view.h
	$(CC) $(CFLAGS) -c SpringIntoView/spring_into_view.c -o SpringIntoView/spring_into_view.o

SpringIntoView/stb_truetype_impl.o: SpringIntoView/stb_truetype_impl.c
	$(CC) $(CFLAGS) -c SpringIntoView/stb_truetype_impl.c -o SpringIntoView/stb_truetype_impl.o

ASM_OBJS = isr_asm.o
C_SRCS = kernel.c isr.c idt.c pic.c pmm.c pit.c keyboard.c serial.c string.c vfs.c initrd.c heap.c vmm.c mouse.c vbe.c bochs_vbe.c SpringIntoView/spring_into_view.c SpringIntoView/stb_truetype_impl.c
OBJS = $(C_SRCS:.c=.o) $(ASM_OBJS)

$(KERNEL): $(OBJS) multiboot_header.o kernel_entry.o
	$(LD) $(LDFLAGS) multiboot_header.o kernel_entry.o $(OBJS) -o $(KERNEL)

$(ISO): $(BOOT) $(KERNEL) $(INITRD)
	mkdir -p iso/boot/grub
	cp $(BOOT) iso/boot/
	cp $(KERNEL) iso/boot/
	cp $(INITRD) iso/boot/
	echo 'set timeout=1' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'terminal_output gfxterm' >> iso/boot/grub/grub.cfg
	echo 'menuentry "SentinelOS" {' >> iso/boot/grub/grub.cfg
	echo '  insmod all_video' >> iso/boot/grub/grub.cfg
	echo '  set gfxmode=1920x1080x32' >> iso/boot/grub/grub.cfg
	echo '  set gfxpayload=keep' >> iso/boot/grub/grub.cfg
	echo '  multiboot2 /boot/kernel.bin' >> iso/boot/grub/grub.cfg
	echo '  module2 /boot/initrd.tar initrd.tar' >> iso/boot/grub/grub.cfg
	echo '  boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	/opt/homebrew/opt/i686-elf-grub/bin/i686-elf-grub-mkrescue -o $(ISO) iso

clean:
	rm -f *.o *.bin *.iso
	rm -rf iso

.PHONY: all clean

.c.o:
	$(CC) $(CFLAGS) $< -o $@