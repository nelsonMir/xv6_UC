# Definir la plataforma
platform := vf2

# Definir rutas de kernel y usuario
K = kernel
U = user
T = target

# Definir objetos del kernel
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
  $(K)/virtio_disk.o \
  $(K)/schedulers.o

# Herramientas de compilación
TOOLPREFIX := riscv64-unknown-elf-
CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)as
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# Flags de compilación
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD -mcmodel=medany
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I$(CURDIR)/kernel -I$(CURDIR)/user  # Rutas relativas
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Linker script específico para la VF2
ifeq ($(platform), vf2)
linker = ./linker/vf2.ld
endif

# Enlazar el kernel (sin initcode.o)
$(T)/kernel.bin: $(OBJS) $(linker)
	@if [ ! -d "$(T)" ]; then mkdir $(T); fi
	@$(LD) -T $(linker) -o $(T)/kernel $(OBJS)
	@$(OBJDUMP) -S $(T)/kernel > $(T)/kernel.asm
	@$(OBJDUMP) -t $(T)/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(T)/kernel.sym
	@$(OBJCOPY) -O binary $(T)/kernel $(T)/kernel.bin

# Compilación de initcode por separado
$(U)/initcode.o: $(U)/initcode.S
	@echo "Compilando initcode con las rutas: $(CFLAGS)"
	$(CC) $(CFLAGS) -c $< -o $@

$(U)/initcode: $(U)/initcode.o
	$(LD) -T $(linker) -o $@ $^

# Compilación de programas de usuario
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

# Crear imagen de sistema de archivos (usa initcode aquí)
fs.img: mkfs/mkfs README $(UPROGS) $(U)/initcode
	mkfs/mkfs fs.img README $(UPROGS)

# Limpieza
clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$(U)/initcode $(U)/initcode.out $(K)/kernel fs.img \
	mkfs/mkfs .gdbinit \
	$(U)/usys.S \
	$(UPROGS)

# Opciones de QEMU
QEMU = qemu-system-riscv64
QEMUOPTS = -machine virt -bios none -kernel $(T)/kernel -m 128M -smp 3 -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: $(T)/kernel fs.img
	$(QEMU) $(QEMUOPTS)
