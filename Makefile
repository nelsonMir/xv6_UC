# Plataforma objetivo
platform := vf2

# Rutas
K = kernel
U = user
T = target

# Herramientas de compilación cruzada
TOOLPREFIX := riscv64-unknown-elf-
CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)as
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# Flags de compilación
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD -mcmodel=medany -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I. -Ikernel -Iuser -I$(CURDIR)/kernel -I$(CURDIR)/user
CFLAGS += -O2 -g -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Linker script
ifeq ($(platform), vf2)
linker = ./linker/vf2.ld
endif

# Objetos del kernel
OBJS = \
  $(K)/entry_vf2.o \
  $(K)/start.o \
  $(K)/console.o \
  $(K)/printf.o \
  $(K)/uart.o \
  $(K)/kalloc.o \
  $(K)/spinlock.o \
  $(K)/string.o \
  $(K)/main.o \
  $(K)/vm.o \
  $(K)/proc.o \
  $(K)/swtch.o \
  $(K)/trampoline.o \
  $(K)/trap.o \
  $(K)/syscall.o \
  $(K)/sysproc.o \
  $(K)/bio.o \
  $(K)/fs.o \
  $(K)/log.o \
  $(K)/sleeplock.o \
  $(K)/file.o \
  $(K)/pipe.o \
  $(K)/exec.o \
  $(K)/sysfile.o \
  $(K)/kernelvec.o \
  $(K)/plic.o \
  $(K)/ramdisk_xv6.o \
  $(K)/schedulers.o 

# Librerías de usuario
ULIB = $(U)/ulib.o $(U)/usys.o $(U)/printf.o $(U)/umalloc.o

# Programas de usuario
UPROGS = \
  $(U)/_cat \
  $(U)/_echo \
  $(U)/_forktest \
  $(U)/_grep \
  $(U)/_init \
  $(U)/_kill \
  $(U)/_ln \
  $(U)/_ls \
  $(U)/_mkdir \
  $(U)/_rm \
  $(U)/_sh \
  $(U)/_stressfs \
  $(U)/_usertests \
  $(U)/_grind \
  $(U)/_wc \
  $(U)/_zombie \
  $(U)/_sleep \
  $(U)/_freemem \
  $(U)/_pagesize \
  $(U)/_ps \
  $(U)/_getpriority \
  $(U)/_nice \
  $(U)/_pwd

# Kernel binario
$(T)/kernel.bin: $(OBJS) $(linker) fs.img
	@mkdir -p $(T)
	$(LD) -T $(linker) -o $(T)/kernel $(OBJS)
	$(OBJDUMP) -S $(T)/kernel > $(T)/kernel.asm
	$(OBJDUMP) -t $(T)/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(T)/kernel.sym
	$(OBJCOPY) -O binary $(T)/kernel $(T)/kernel.bin

$(U)/initcode.o: $(U)/initcode.S
	$(AS) -o $@ $<

$(U)/initcode.elf: $(U)/initcode.o $(U)/initcode.ld
	$(LD) -T $(@D)/initcode.ld -o $@ $<

$(U)/initcode.out: $(U)/initcode.elf
	$(OBJCOPY) -O binary -j .text -j .data $< $@

kernel/initcode.h: $(U)/initcode.out
	xxd -i $< | sed 's/user_initcode_out/initcode/g; s/unsigned int initcode_len;/unsigned int initcode_len = sizeof(initcode);/' > $@

$(K)/proc.o: kernel/initcode.h

$(U)/_init: $(U)/init.o $(ULIB)
	$(LD) -T $(U)/user.ld -o $@ $^
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

fs.img: mkfs/mkfs README $(UPROGS) $(U)/initcode.out
	cp $(U)/_init $(U)/init
	mkfs/mkfs fs.img README $(UPROGS) $(U)/init

# Reglas para user libs
$(U)/usys.S: $(U)/usys.pl
	perl $< > $@

$(U)/usys.o: $(U)/usys.S
	$(CC) $(CFLAGS) -c -o $@ $<

$(U)/ulib.o: $(U)/ulib.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(U)/printf.o: $(U)/printf.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(U)/umalloc.o: $(U)/umalloc.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(U)/init.o: $(U)/init.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Reglas para compilar programas de usuario
$(U)/_%: $(U)/%.o $(ULIB)
	$(LD) -T $(U)/user.ld -o $@ $^
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(U)/%.o: $(U)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Imagen de sistema de archivos
fs.img: mkfs/mkfs README $(UPROGS) $(U)/initcode.out
	cp $(U)/_init $(U)/init
	mkfs/mkfs fs.img README $(UPROGS) $(U)/init

# Generar kernel/fsimg.h automáticamente desde fs.img
kernel/fsimg.h: fs.img
	@echo "Convirtiendo fs.img a fsimg.h..."
	@xxd -i fs.img > kernel/fsimg.h

# Asegurarse de que ramdisk_xv6.o dependa de fsimg.h
$(K)/ramdisk_xv6.o: $(K)/ramdisk_xv6.c kernel/fsimg.h
	$(CC) $(CFLAGS) -c -o $@ $<

# mkfs
mkfs/mkfs: mkfs/mkfs.c
	gcc -Wall -Werror -Wno-stringop-truncation -O2 -I. -o $@ $<

# Limpieza
clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$(U)/initcode $(U)/initcode.out $(T)/kernel $(T)/kernel.bin fs.img \
	kernel/fsimg.h mkfs/mkfs .gdbinit $(U)/usys.S $(UPROGS) \
	kernel/initcode.h \
	$(U)/init

# QEMU
QEMU = qemu-system-riscv64
QEMUOPTS = -machine virt -bios none -kernel $(T)/kernel -m 128M -smp 3 -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: $(T)/kernel fs.img
	$(QEMU) $(QEMUOPTS)
