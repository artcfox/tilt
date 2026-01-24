###############################################################################
# Makefile for Tilt Puzzle
###############################################################################

## General Flags
PROJECT = Tilt
GAME= tilt
MCU = atmega644
TARGET = $(GAME).elf
CC = avr-gcc
INFO= gameinfo.properties

## Location of packrom
UZEBIN_DIR=../../bin

## Escape spaces in mixer path (for including a custom sounds.inc)
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
SPACE_ESC := \\$(SPACE)
MIX_PATH := $(realpath data/sounds.inc)
MIX_PATH_ESC := $(subst $(SPACE),$(SPACE_ESC),$(MIX_PATH))

## Kernel settings
KERNEL_DIR = ../../kernel
KERNEL_OPTIONS  = -DVIDEO_MODE=3
KERNEL_OPTIONS += -DINTRO_LOGO=0
KERNEL_OPTIONS += -DSCROLLING=0
KERNEL_OPTIONS += -DSOUND_MIXER=1
KERNEL_OPTIONS += -DSOUND_CHANNEL_5_ENABLE=1
KERNEL_OPTIONS += -DRAM_TILES_COUNT=30
#KERNEL_OPTIONS += -DSCREEN_TILES_V=16
#KERNEL_OPTIONS += -DSCREEN_TILES_H=16
KERNEL_OPTIONS += -DTRANSLUCENT_COLOR=0xf6
KERNEL_OPTIONS += -DVRAM_TILES_V=28
KERNEL_OPTIONS += -DVRAM_TILES_H=28
KERNEL_OPTIONS += -DOVERLAY_LINES=0
KERNEL_OPTIONS += -DRESOLUTION_EXT=1
#KERNEL_OPTIONS += -DRT_ALIGNED=1
#KERNEL_OPTIONS += -DSPRITES_AUTO_PROCESS=0
#KERNEL_OPTIONS += -DCONTROLLERS_VSYNC_READ=0
#KERNEL_OPTIONS += -DMUSIC_ENGINE=STREAM
#KERNEL_OPTIONS += -DMIXER_WAVES=\"$(MIX_PATH_ESC)\"


## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -Wextra -Winline -Werror=vla -gdwarf-2 -std=gnu99 -DF_CPU=28636360UL -Os -fsigned-char
CFLAGS += -ffunction-sections -fno-toplevel-reorder -fno-tree-switch-conversion
CFLAGS += -mstrict-X -maccumulate-args
CFLAGS += -MD -MP -MT $(*F).o -MF $(@F).d
CFLAGS += $(KERNEL_OPTIONS)


## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS += -Wl,-Map=$(GAME).map
LDFLAGS += -Wl,-gc-sections

## The next line is only for video mode 3 with scrolling. Adjust the .data value to be 0x800100+VRAM_TILES_H*VRAM_TILES_V
#LDFLAGS += -Wl,--section-start,.noinit=0x800100 -Wl,--section-start,.data=0x800500
## If RT_ALIGNED=1, then the LDFLAGS need to be set to what's below
#LDFLAGS += -Wl,--section-start,.noinit=0x800100 -Wl,--section-start,.data=0x800D00


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings


## Objects that must be built in order to link (prefix with .)
OBJECTS  = .uzeboxVideoEngineCore.o
OBJECTS += .uzeboxCore.o
OBJECTS += .uzeboxSoundEngine.o
OBJECTS += .uzeboxSoundEngineCore.o
OBJECTS += .uzeboxVideoEngine.o
OBJECTS += .$(GAME).o
#OBJECTS += .stackmon.o


## Include Directories
INCLUDES = -I"$(KERNEL_DIR)"

## Makefile dependencies
DEPS  = Makefile

## Build
all: ./data/tileset.inc ./data/spriteset.inc ./data/midisong.inc $(TARGET) $(GAME).hex $(GAME).eep $(GAME).lss $(GAME).uze

## Compile Kernel files (prefix with .)
.uzeboxVideoEngineCore.o: $(KERNEL_DIR)/uzeboxVideoEngineCore.s $(DEPS)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@

.uzeboxSoundEngineCore.o: $(KERNEL_DIR)/uzeboxSoundEngineCore.s $(DEPS)
	$(CC) $(INCLUDES) $(ASMFLAGS) -c $< -o $@

.uzeboxCore.o: $(KERNEL_DIR)/uzeboxCore.c $(DEPS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

.uzeboxSoundEngine.o: $(KERNEL_DIR)/uzeboxSoundEngine.c $(DEPS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

.uzeboxVideoEngine.o: $(KERNEL_DIR)/uzeboxVideoEngine.c $(DEPS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

## Compile game sources (prefix with .)
.$(GAME).o: $(GAME).c $(DEPS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

.stackmon.o: stackmon.c $(DEPS)
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

./data/tileset.inc: ./data/tileset.png ./data/tileset.xml
	$(UZEBIN_DIR)/gconvert ./data/tileset.xml

./data/spriteset.inc: ./data/spriteset.png ./data/spriteset.xml
	$(UZEBIN_DIR)/gconvert ./data/spriteset.xml

./data/midisong.h: ./data/midisong.mid $(DEPS)
	$(UZEBIN_DIR)/midiconv -f 8 ./data/midisong.mid ./data/midisong.h

./data/midisong.inc: ./data/midisong.h ./data/mconvert.cfg
	$(UZEBIN_DIR)/mconvert ./data/mconvert.cfg

## Link
$(TARGET): $(OBJECTS) $(DEPS)
	 $(CC) $(LDFLAGS) $(OBJECTS) $(LIBDIRS) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS) $< $@
	avr-size -A --format=avr --mcu=$(MCU) $^

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

%.uze: $(TARGET)
	-$(UZEBIN_DIR)/packrom $(GAME).hex $@ $(INFO)
	-md5sum $(GAME).uze

## Clean target
.PHONY: clean
clean:
	-rm -rf ./data/tileset.inc ./data/spriteset.inc ./data/midisong.h ./data/midisong.inc $(OBJECTS) $(TARGET) $(GAME).eep $(GAME).hex $(GAME).lss $(GAME).map $(GAME).uze $(OBJECTS:.o=.o.d)

## Proper automatic dependency tracking requires the *.o and *.o.d files to be
## generated in the top level directory, so we hide the *.o and *.o.d files
## by making sure the object filenames begin with a '.' above
-include .*.d
