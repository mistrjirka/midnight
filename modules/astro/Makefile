# Magic Lantern modules are architecture-independent ARMv5TE relocatable objects.
MODULE_NAME = astro
MODULE_OBJS = $(BUILD_DIR)/astro.o $(BUILD_DIR)/astro_core.o $(BUILD_DIR)/astro_focus_control.o $(BUILD_DIR)/astro_shortcut.o $(BUILD_DIR)/astro_streak.o $(BUILD_DIR)/astro_psf.o $(BUILD_DIR)/astro_sky.o
TOP_DIR = ../..
include $(TOP_DIR)/modules/Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete
