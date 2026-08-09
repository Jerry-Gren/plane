ROOT_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

CC := x86_64-elf-gcc
LD := x86_64-elf-ld
HOSTCC ?= gcc
GENERATED_DIR := include/generated
AUTOCONF_HEADER := $(GENERATED_DIR)/autoconf.h
BUILD_MODE := $(if $(filter 1,$(DEBUG)),debug,release)
BUILD_MODE_FILE := build/.build_mode
.DEFAULT_GOAL := all

override CFLAGS += \
	-I$(ROOT_DIR)/include \
	-I$(ROOT_DIR)/hal \
	-std=gnu11 \
	-Wall -Wextra -Werror \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-PIC \
	-m64 \
	-march=x86-64 \
	-mabi=sysv \
	-mcmodel=kernel \
	-mno-80387 \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mno-red-zone

override HOSTCFLAGS += \
	-I$(ROOT_DIR)/include \
	-I$(ROOT_DIR)/hal \
	-std=gnu11 \
	-Wall -Wextra -Werror

override ASFLAGS += -Wa,--divide

ifeq ($(DEBUG),1)
override CFLAGS += -g3 -Og -fno-omit-frame-pointer
override HOSTCFLAGS += -g3 -O0
endif

-include .config

SRC_DIRS := kernel kernel/mm klib

ifeq ($(CONFIG_X86_64),y)
    SRC_DIRS += hal/x86_64
endif

ifeq ($(CONFIG_BOOT_LIMINE),y)
    SRC_DIRS += boot/limine
	ifeq ($(CONFIG_X86_64),y)
        SRC_DIRS += hal/x86_64/boot/limine
    endif
    LINKER_SCRIPT := hal/x86_64/linker_limine.lds
endif

ifeq ($(CONFIG_BOOT_GRUB),y)
    SRC_DIRS += boot/multiboot2
	ifeq ($(CONFIG_X86_64),y)
        SRC_DIRS += hal/x86_64/boot/multiboot2
    endif
    LINKER_SCRIPT := hal/x86_64/linker_grub.lds
endif

C_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
ALL_S_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.S))
S_FILES := $(filter-out %.lds.S, $(ALL_S_FILES))
OBJS := $(C_FILES:.c=.o) $(S_FILES:.S=.o)
DEPS := $(OBJS:.o=.d) $(LINKER_SCRIPT:.lds=.lds.d)
KERNEL := plane.elf
TEST_SRCS := $(wildcard tests/*_test.c)
TEST_MKS := $(wildcard tests/*_test.mk)
TEST_BINS := $(patsubst tests/%.c,build/tests/%,$(TEST_SRCS))
PUBLIC_HEADERS := include/plane/*.h include/hal/*.h
ARCH_TEST_HEADERS := include/hal/x86_64/*.h
TEST_SUPPORT_HEADERS := tests/support/*.h
INTERNAL_TEST_HEADERS := \
	kernel/mm/kmem_internal.h \
	kernel/mm/vm_map_internal.h \
	kernel/mm/vm_object_internal.h \
	kernel/mm/vm_page_internal.h \
	kernel/mm/vm_zone_internal.h

-include $(TEST_MKS)
-include $(DEPS)

.SECONDEXPANSION:

.PHONY: all clean debug debug-iso debug-clean menuconfig iso qemu qemu-gdb check unit-check FORCE

all: $(KERNEL)

$(BUILD_MODE_FILE): FORCE
	@mkdir -p $(dir $@)
	@if [ ! -f $@ ] || [ "$$(cat $@)" != "$(BUILD_MODE)" ]; then \
		echo "  CLEAN   $(BUILD_MODE) objects"; \
		find kernel klib hal boot drivers -type f -name "*.o" -delete; \
		find kernel klib hal boot drivers -type f -name "*.d" -delete; \
		find hal -type f -name "*.lds" -delete; \
		rm -f $(KERNEL); \
		echo "$(BUILD_MODE)" > $@; \
	fi

$(KERNEL): $(OBJS) $(LINKER_SCRIPT)
	@echo "  LD      $@"
	@$(LD) -T $(LINKER_SCRIPT) $(OBJS) -o $@

%.lds: %.lds.S $(BUILD_MODE_FILE)
	@echo "  CPP     $@"
	@$(CC) -E -P -x c -D__ASSEMBLER__ $(CFLAGS) \
		-MMD -MP -MT $@ -MF $@.d $< -o $@

%.o: %.c $(AUTOCONF_HEADER) $(BUILD_MODE_FILE)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

%.o: %.S $(BUILD_MODE_FILE)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(AUTOCONF_HEADER): .config
	@mkdir -p $(GENERATED_DIR)
	@genconfig --header-path $(AUTOCONF_HEADER)

menuconfig:
	@mkdir -p $(GENERATED_DIR)
	@MENUCONFIG_STYLE="monochrome" menuconfig
	@genconfig --header-path $(AUTOCONF_HEADER)

check: unit-check

debug:
	@$(MAKE) DEBUG=1 all

debug-iso:
	@$(MAKE) DEBUG=1 iso

debug-clean:
	@echo "  CLEAN   debug objects"
	@find kernel klib hal boot drivers -type f -name "*.o" -delete
	@find kernel klib hal boot drivers -type f -name "*.d" -delete
	@find hal -type f -name "*.lds" -delete
	@rm -f $(KERNEL)

unit-check: $(TEST_BINS)
	@for test in $(TEST_BINS); do \
		echo "  TEST    $$test"; \
		$$test || exit $$?; \
	done

build/tests/%_test: tests/%_test.c $(PUBLIC_HEADERS) $(ARCH_TEST_HEADERS) $(TEST_SUPPORT_HEADERS) $(INTERNAL_TEST_HEADERS) $$($$*_test_DEPS) $$($$*_test_PREREQS)
	@echo "  HOSTCC  $@"
	@mkdir -p $(dir $@)
	@$(HOSTCC) $(HOSTCFLAGS) $< $($*_test_DEPS) -o $@

# ISO / QEMU
ISO_NAME := plane.iso
ISO_DIR  := build/iso_root
ifeq ($(CONFIG_BOOT_LIMINE),y)
LIMINE_EXE := tools/limine_bin/limine
$(LIMINE_EXE):
	@echo "  HOSTCC  Limine Deploy Tool"
	@$(MAKE) -s -C tools/limine_bin
iso: $(KERNEL) $(LIMINE_EXE)
	@echo "  MKISO   $(ISO_NAME) [Limine]"
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/limine
	@mkdir -p $(ISO_DIR)/EFI/BOOT
	@cp $(KERNEL) $(ISO_DIR)/boot/
	@cp tools/limine_bin/limine-bios.sys $(ISO_DIR)/boot/limine/
	@cp tools/limine_bin/limine-bios-cd.bin $(ISO_DIR)/boot/limine/
	@cp tools/limine_bin/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/
	@cp tools/limine_bin/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	@cp tools/limine_bin/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/
	@echo "timeout: 3" > $(ISO_DIR)/boot/limine/limine.conf
	@echo "/PlanE" >> $(ISO_DIR)/boot/limine/limine.conf
	@echo "    protocol: limine" >> $(ISO_DIR)/boot/limine/limine.conf
	@echo "    path: boot():/boot/$(KERNEL)" >> $(ISO_DIR)/boot/limine/limine.conf
	@xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_NAME) > /dev/null 2>&1
	@$(LIMINE_EXE) bios-install $(ISO_NAME) > /dev/null 2>&1
endif

ifeq ($(CONFIG_BOOT_GRUB),y)
iso: $(KERNEL)
	@echo "  MKISO   $(ISO_NAME) [GRUB/Multiboot2]"
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/
	@# GEN grub.cfg
	@echo "set timeout=3" > $(ISO_DIR)/boot/grub/grub.cfg
	@echo "insmod all_video" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "set gfxmode=auto" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "set gfxpayload=keep" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "menuentry \"PlanE\" {" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    multiboot2 /boot/$(KERNEL)" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    boot" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "}" >> $(ISO_DIR)/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO_NAME) $(ISO_DIR) > /dev/null 2>&1
endif

qemu: iso
	@echo "  QEMU    $(ISO_NAME)"
	@qemu-system-x86_64 -M q35 -m 2G -cdrom $(ISO_NAME) -boot d -serial stdio

qemu-gdb: debug-iso
	@echo "  QEMU    $(ISO_NAME) [GDB]"
	@qemu-system-x86_64 -M q35 -m 2G -cdrom $(ISO_NAME) -boot d \
		-serial stdio -display none -no-reboot -S -s

clean:
	@echo "  CLEAN"
	@find kernel klib hal boot drivers -type f -name "*.o" -delete
	@find kernel klib hal boot drivers -type f -name "*.d" -delete
	@find hal -type f -name "*.lds" -delete
	@rm -f $(KERNEL) $(ISO_NAME)
	@rm -rf $(ISO_DIR) build/tests

distclean: clean
	@echo "  DISTCLEAN"
	@rm -rf build/ $(GENERATED_DIR)/* .config .config.old
	@$(MAKE) -s -C tools/limine_bin clean
