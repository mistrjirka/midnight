# AstroML module. Built from inside a Magic Lantern tree at modules/astro (scripts/build.sh
# copies it there). Modules are architecture-independent ARMv5TE relocatable objects.
MODULE_NAME = astro
MODULE_SOURCES = $(wildcard *.c) $(wildcard core/*.c)
MODULE_OBJS = $(addprefix $(BUILD_DIR)/,$(MODULE_SOURCES:.c=.o))
TOP_DIR = ../..
include $(TOP_DIR)/modules/Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

# ML's compile rule only creates build/ itself.
$(filter $(BUILD_DIR)/core/%,$(MODULE_OBJS)): | $(BUILD_DIR)/core
$(BUILD_DIR)/core:
	@mkdir -p $@
