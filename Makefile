# Dreamcast (KallistiOS) build.
#
# Usage:
#   source /opt/toolchains/dc/kos/environ.sh
#   make -f Makefile.dc
#
# Produces rayverse.elf. See doc/DREAMCAST.md for how to master a disc image
# with the game data and the Red Book audio tracks.

TARGET = rayverse.elf
OBJS = src/rayverse.o src/stb_vorbis.o

KOS_CFLAGS += -std=gnu11 -Wall -Wno-unused-variable -Wno-unused-but-set-variable

# Music backend: 0 = stream .ogg files from the data track (default),
# 1 = Red Book CDDA via the GD-ROM drive (see tools/make_cdi.sh MUSIC=cdda)
DC_MUSIC_CDDA ?= 0
KOS_CFLAGS += -DDC_MUSIC_CDDA=$(DC_MUSIC_CDDA)
# libtremor keeps its headers in its own inst dir instead of the shared include
KOS_CFLAGS += -I$(KOS_PORTS)/libtremor/inst/include

all: $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS) -ltremor -lm

.PHONY: all clean
