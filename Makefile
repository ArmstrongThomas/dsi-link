# DSi Link receiver. Build with BlocksDS installed (`make`), or via scripts/build-docker.*.
BLOCKSDS ?= /opt/blocksds/core
ARM7ELF := $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf
NAME := DSiLink
GAME_TITLE := DSi Link
GAME_SUBTITLE := Wi-Fi build receiver 1.2
GAME_AUTHOR := Tommy
GAME_ICON := icon.gif
SOURCEDIRS := source
INCLUDEDIRS := source
CFLAGS := -Wall -Wextra -Werror -Wno-misleading-indentation -std=gnu11
LIBS := -ldswifi9 -lnds9
LIBDIRS := $(BLOCKSDS)/libs/dswifi
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
