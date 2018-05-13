ARCH_INLUCDES := debug driver libs mm numa process sync trap syscall kmodule
ARCH_CFLAGS := -DARCH_RISCV -mcmodel=medany -std=gnu99 -Wno-unused \
				-fno-builtin -Wall -O2 -nostdinc \
				-fno-stack-protector -ffunction-sections -fdata-sections
ARCH_LDFLAGS := -m elf32lriscv -nostdlib
