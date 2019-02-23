USE_GZIP_COMPRESSION ?= no
USE_HEATSHRINK ?= yes

THISDIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CFLAGS=-I$(THISDIR)../heatshrink/src -I$(THISDIR)../heatshrink/include -I$(THISDIR)../include -I$(THISDIR)/.. -std=gnu99
ifeq ("$(USE_GZIP_COMPRESSION)","yes")
CFLAGS		+= -DESPFS_GZIP
endif

ifeq ("$(USE_HEATSHRINK)","yes")
CFLAGS		+= -DESPFS_HEATSHRINK
endif

OBJS=main.o heatshrink_encoder.o
TARGET=mkespfsimage


%.o: $(THISDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $^

%.o: $(THISDIR)../heatshrink/src/%.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(TARGET): $(OBJS)
ifeq ("$(USE_GZIP_COMPRESSION)","yes")
	$(CC) -o $@ $^ -lz
else
	$(CC) -o $@ $^
endif

clean:
	rm -f $(TARGET) $(OBJS)
