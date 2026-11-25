# Building ojay programs: the compilers produce object files; linking is done
# with each platform's normal linker against the ojay runtime.
#
#   make            hello (x86-64 Linux), hello-mcxn947.elf (MCX N947 flash
#                   image) and hello-esp32c6.elf (ESP32-C6 SRAM image)
#   make hello-esp32c6-app.bin   ESP32-C6 application for the ESP-IDF bootloader
#                                (flash with: esptool write-flash 0x10000 <file>)
#   make hello-mcxn947-ram.elf   MCX N947 image to load into SRAM with a debugger
#   make blink-mcxn947.elf       LED blinker for the FRDM-MCXN947 (flash image)
#
# Set OJAY to the build (or install) prefix.

OJAY    ?= ../build
OJAYLIB := $(OJAY)/lib/ojay

ARM_CC    := arm-none-eabi-gcc
ARM_FLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
ARM_LINK  := $(ARM_CC) $(ARM_FLAGS) -nostdlib -nostartfiles -Wl,--gc-sections \
             -Wl,--no-warn-rwx-segments -Wl,--no-warn-execstack -L$(OJAYLIB)/boards
ARM_LIBS  := $(OJAYLIB)/libojayrt_arm32_baremetal.a -Wl,--start-group -lm -lc_nano -lgcc -Wl,--end-group

# riscv32-esp-elf-gcc from ESP-IDF's tools (on PATH, or the newest installed)
RV_CC     ?= $(or $(shell command -v riscv32-esp-elf-gcc),$(lastword $(sort $(wildcard \
               $(HOME)/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc))))
RV_LINK   := $(RV_CC) -march=rv32imac_zicsr_zifencei -mabi=ilp32 -nostdlib -nostartfiles \
             -Wl,--gc-sections -L$(OJAYLIB)/boards
RV_LIBS   := $(OJAYLIB)/libojayrt_riscv32_esp32c6.a -Wl,--start-group -lm -lc_nano -lgcc -Wl,--end-group

all: hello hello-mcxn947.elf hello-esp32c6.elf blink-mcxn947.elf

# ---- x86-64 Linux
%.x86_64.o: %.jai
	$(OJAY)/bin/ojay-x86_64 -O2 $< -o $@

hello: hello.x86_64.o
	cc $< $(OJAYLIB)/libojayrt_x86_64_hosted.a -lm -o $@

# ---- ARM Cortex-M33 (NXP MCX N947)
%.arm32.o: %.jai
	$(OJAY)/bin/ojay-arm32 -O2 $< -o $@

hello-mcxn947.elf: hello.arm32.o
	$(ARM_LINK) -T mcxn947-flash.ld $< $(ARM_LIBS) -o $@

blink-mcxn947.elf: blink.arm32.o
	$(ARM_LINK) -T mcxn947-flash.ld $< $(ARM_LIBS) -o $@

blink-mcxn947-ram.elf: blink.arm32.o
	$(ARM_LINK) -T mcxn947-ram.ld $< $(ARM_LIBS) -o $@

hello-mcxn947-ram.elf: hello.arm32.o
	$(ARM_LINK) -T mcxn947-ram.ld $< $(ARM_LIBS) -o $@

# ---- RISC-V (ESP32-C6)
%.riscv.o: %.jai
	$(OJAY)/bin/ojay-riscv -O2 $< -o $@

hello-esp32c6.elf: hello.riscv.o
	$(RV_LINK) -T esp32c6.ld $< $(RV_LIBS) -o $@

hello-esp32c6-app.elf: hello.riscv.o
	$(RV_LINK) -T esp32c6-app.ld $< $(RV_LIBS) -o $@

%.bin: %.elf
	esptool --chip esp32c6 elf2image --output $@ $<

clean:
	rm -f *.o hello *.elf *.bin

.PHONY: all clean
