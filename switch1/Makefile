# SPDX-License-Identifier: GPL-3.0-or-later
#
# Switch 1 build of OunceShell: same portable sources and platform layer as
# switch2/Makefile, but against plain devkitA64 (libnx + devkitPro portlibs)
# instead of the Switch 2 devkit (libounce + OunceVK). SDL 2's Render
# benchmark backend drives the GL path on both consoles.
#
#   make -f switch1/Makefile    ->  build/switch1/ounceshell.nro
#
# In devkitPro's Docker image:
#
#   docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64 make -f switch1/Makefile
DEVKITPRO ?= /opt/devkitpro
LIBNX := $(DEVKITPRO)/libnx
PORTLIBS := $(DEVKITPRO)/portlibs/switch
TOOLS := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
include $(LIBNX)/switch_rules

OUT := build/switch1

NAME := ounceshell
TITLE := OunceShell
AUTHOR := TheFloW (VitaShell), kurumuz
VERSION := 2.0-switch2
ICON := switch2/resources/icon.jpg

UPSTREAM_SOURCES := \
	main.c main_context.c browser.c utils.c file.c \
	archive.c photo.c audioplayer.c text.c hex.c \
	settings.c config.c language.c theme.c \
	uncommon_dialog.c message_dialog.c property_dialog.c \
	context_menu.c io_process.c makezip.c sfo.c bm.c \
	strnatcmp.c sha1.c \
	minizip/zip.c minizip/ioapi.c \
	audio/player.c audio/id3.c audio/oggplayer.c \
	audio/mp3player.c audio/mp3xing.c audio/lrcparse.c \
	libmad/bit.c libmad/decoder.c libmad/fixed.c libmad/frame.c \
	libmad/huffman.c libmad/layer12.c libmad/layer3.c \
	libmad/stream.c libmad/synth.c libmad/timer.c

PORT_SOURCES := \
	switch2/src/psp2_io.c switch2/src/psp2_kernel.c \
	switch2/src/psp2_rtc.c switch2/src/psp2_ctrl.c \
	switch2/src/psp2_misc.c switch2/src/vita2d_sdl.c \
	switch2/src/vita2d_font.c switch2/src/ime_dialog.c \
	switch2/src/vita_audio_sdl.c switch2/src/init.c \
	switch2/src/dropped_features.c switch2/src/psp2_net.c \
	switch2/src/ftp_server.c switch2/third_party/ftpd/ftp.c

RESOURCES := \
	resources/english_us.txt resources/theme.txt \
	resources/default/colors.txt \
	switch2/resources/NotoSans-Regular.ttf \
	$(sort $(wildcard resources/default/*.png))

ARCH := -march=armv8-a+crc -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu11 -O2 -funsigned-char $(ARCH) \
	-ffunction-sections -fdata-sections \
	-Wall -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-format-truncation \
	-D__SWITCH__ -D__SWITCH2__ -D_REENTRANT \
	-Iswitch2/include -Iswitch2/third_party/ftpd -iquote . \
	-I$(PORTLIBS)/include/SDL2 -I$(PORTLIBS)/include \
	-isystem $(LIBNX)/include

LIBPATHS := -L$(PORTLIBS)/lib -L$(LIBNX)/lib
LIBS := -lSDL2_ttf -lharfbuzz -lfreetype \
	-lSDL2_image -lpng16 -ljpeg -lwebpdemux -lwebp \
	-larchive -lexpat -llz4 -lzstd -llzma -lbz2 -lz \
	-lvorbisfile -lvorbis -logg \
	-lonig \
	-lSDL2 -lEGL -lglapi -ldrm_nouveau -lstdc++ -lpthread \
	-lnx -lm

object = $(OUT)/obj/$(subst /,__,$(basename $(1))).o
resource_object = $(OUT)/res/$(subst /,__,$(1)).o
OBJECTS := $(foreach s,$(UPSTREAM_SOURCES) $(PORT_SOURCES),$(call object,$(s)))
RESOURCE_OBJECTS := $(foreach r,$(RESOURCES),$(call resource_object,$(r)))

all: $(OUT)/$(NAME).nro

define compile
$(call object,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	$(TOOLS)gcc $(CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach s,$(UPSTREAM_SOURCES) $(PORT_SOURCES),$(eval $(call compile,$(s))))

define embed
$(call resource_object,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	$(TOOLS)ld -r -b binary -o $$@ $(1)
endef
$(foreach r,$(RESOURCES),$(eval $(call embed,$(r))))

$(OUT)/$(NAME).elf: $(OBJECTS) $(RESOURCE_OBJECTS)
	$(TOOLS)gcc -specs=$(LIBNX)/switch.specs -g $(ARCH) \
	    -Wl,-Map,$(OUT)/$(NAME).map $^ $(LIBPATHS) $(LIBS) -o $@

$(OUT)/$(NAME).nacp: switch1/Makefile
	@mkdir -p $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create "$(TITLE)" "$(AUTHOR)" "$(VERSION)" $@

$(OUT)/$(NAME).nro: $(OUT)/$(NAME).elf $(OUT)/$(NAME).nacp $(ICON)
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/$(NAME).nacp --icon=$(ICON)

clean:
	rm -rf $(OUT)

.PHONY: all clean

-include $(OBJECTS:.o=.d)
