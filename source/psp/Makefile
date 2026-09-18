TARGET = sonicr_psp

PSPDEV ?= $(shell psp-config --pspdev-path)
PSPSDK ?= $(shell psp-config --pspsdk-path)

SRCDIR = src

EXCLUDE = \
	$(SRCDIR)/platform/platform_sdl.c \
	$(SRCDIR)/render_gl.c \
	$(SRCDIR)/r_gl_backend.c \
	$(SRCDIR)/r_soft_backend.c \
	$(SRCDIR)/sound/music_sdl.c \
	$(SRCDIR)/sound/music_rwops.c \
	$(SRCDIR)/sound/sfx_sdl.c \
	$(shell find $(SRCDIR)/net -name '*.c')

CFILES = $(filter-out $(EXCLUDE),$(shell find $(SRCDIR) -name '*.c'))
OBJS = $(CFILES:.c=.o)

INCDIR = $(SRCDIR) $(SRCDIR)/sound $(SRCDIR)/net
CFLAGS = -std=gnu11 -O3 -G0 -fomit-frame-pointer -fno-math-errno -fno-trapping-math -Wall -Wextra -fno-common \
	-DSONICR_PSP -DSONICR_FLOAT32 \
	-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function \
	-Wno-parentheses -Wno-misleading-indentation -Wno-strict-aliasing \
	-Wno-unused-but-set-variable -Wno-implicit-fallthrough \
	-Wno-pointer-sign -Wno-sign-compare -Wno-missing-field-initializers

LIBS = -lpsppower -lpspgu -lpspaudio -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Sonic R PSP
PSP_EBOOT_ICON = ICON0.png
PSP_EBOOT_PIC1 = PIC1.png

include $(PSPSDK)/lib/build.mak

.PHONY: $(TARGET).elf

ifeq ($(BUILD_PRX),1)
all: build/$(TARGET).prx

build/$(TARGET).elf: $(TARGET).elf
	mkdir -p build
	cp -f $< $@

build/$(TARGET).prx: $(TARGET).prx build/$(TARGET).elf
	mkdir -p build
	cp -f $< $@
endif
