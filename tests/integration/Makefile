CROSS ?= riscv64-unknown-elf-
CC := $(CROSS)gcc
READELF := $(CROSS)readelf
BUILD_DIR ?= ../../build/guest
GUEST_CFLAGS := -march=rv64i_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax -nostdlib -nostartfiles
GUEST_LDFLAGS := -T linker.ld -Wl,--build-id=none,--no-relax

.PHONY: all inspect clean

all: $(BUILD_DIR)/hello.elf $(BUILD_DIR)/loop.elf $(BUILD_DIR)/isolation.elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.elf: %.S linker.ld | $(BUILD_DIR)
	$(CC) $(GUEST_CFLAGS) $(GUEST_LDFLAGS) $< -o $@

$(BUILD_DIR)/isolation.elf: isolation.S isolation.ld | $(BUILD_DIR)
	$(CC) $(GUEST_CFLAGS) -T isolation.ld -Wl,--build-id=none,--no-relax $< -o $@

inspect: all
	$(READELF) -h -l $(BUILD_DIR)/hello.elf

clean:
	rm -rf $(BUILD_DIR)
