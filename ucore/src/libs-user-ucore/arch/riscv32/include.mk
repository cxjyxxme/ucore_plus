ARCH_CFLAGS := -DARCH_RISCV
ARCH_LDFLAGS := -m elf32lriscv -nostdlib
ARCH_OBJS := clone.o syscall.o
ARCH_INITCODE_OBJ := initcode.o
