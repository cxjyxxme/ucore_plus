ARCH_INLUCDES := . debug driver include libs mm numa process sync trap syscall smp kmodule driver/acpica/source/include
ARCH_CFLAGS := -DARCH_RISCV64 -D__UCORE_64__ -mcmodel=medany -std=gnu99 -Wextra\
				-fno-builtin -Wall -O2 -nostdinc \
				-fno-stack-protector -ffunction-sections -fdata-sections\
				-D__PERCPU_H -D__GLUE_UCORE_MP_H__ -D__MM_REFCACHE_H
ARCH_LDFLAGS := -m elf64lriscv -nostdlib
