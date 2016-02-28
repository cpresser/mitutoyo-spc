# Based on LUFA Project Makefile www.lufa-lib.org 
# flash-target from http://shackspace.de/wiki/doku.php?id=project:tinymega

LUFA_BASE    = /usr/src/lufa-LUFA-151115/
MCU          = atmega32u4 
ARCH         = AVR8
BOARD        = LEONARDO 
F_CPU        = 16000000
F_USB        = $(F_CPU)
OPTIMIZATION = 3
TARGET       = mitutoyo-spc
SRC          = $(TARGET).c Descriptors.c $(LUFA_SRC_USB) $(LUFA_SRC_USBCLASS)
LUFA_PATH    = $(LUFA_BASE)LUFA/
CC_FLAGS     = -DUSE_LUFA_CONFIG_HEADER -IConfig/ -I$(LUFA_BASE)
LD_FLAGS     =
AVR_PROGRAMMER = dfu-programmer

# Default target
all:

# Include LUFA build script makefiles
include $(LUFA_PATH)/Build/lufa_core.mk
include $(LUFA_PATH)/Build/lufa_sources.mk
include $(LUFA_PATH)/Build/lufa_build.mk
include $(LUFA_PATH)/Build/lufa_cppcheck.mk
include $(LUFA_PATH)/Build/lufa_doxygen.mk
include $(LUFA_PATH)/Build/lufa_dfu.mk
include $(LUFA_PATH)/Build/lufa_hid.mk
include $(LUFA_PATH)/Build/lufa_avrdude.mk
include $(LUFA_PATH)/Build/lufa_atprogram.mk

.phony flash: $(TARGET).hex
	sudo $(AVR_PROGRAMMER) $(MCU) erase
	sudo $(AVR_PROGRAMMER) $(MCU) flash $<
	sudo $(AVR_PROGRAMMER) $(MCU) start ; true
