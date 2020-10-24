COMPONENT_ADD_INCLUDEDIRS := include
COMPONENT_PRIV_INCLUDEDIRS := src heatshrink
COMPONENT_SRCDIRS := src heatshrink
COMPONENT_OBJS := src/espfs.o src/espfs_vfs.o heatshrink/heatshrink_decoder.o

FILES := $(shell find $(PROJECT_PATH)/$(CONFIG_ESPFS_IMAGEROOTDIR) | sed -E 's/([[:space:]])/\\\1/g')

ifeq ("$(CONFIG_ESPFS_LINK_BINARY)","y")
COMPONENT_OBJS += src/espfs_image.o
endif

ifeq ("$(CONFIG_ESPFS_USE_HEATSHRINK)","y")
CFLAGS += -DESPFS_HEATSHRINK
COMPONENT_ADD_INCLUDEDIRS += lib/heatshrink
endif

requirements.stamp:
	python -m pip install -r "${COMPONENT_PATH}/requirements.txt"
	touch requirements.stamp

espfs_image.bin: $(FILES) requirements.stamp
	PROJECT_DIR=${PROJECT_PATH} python "${COMPONENT_PATH}/tools/mkespfsimage.py" "${PROJECT_PATH}/${CONFIG_ESPFS_IMAGEROOTDIR}" espfs_image.bin

src/espfs_image.o: src/espfs_image.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

src/espfs_image.c: espfs_image.bin
	python "${COMPONENT_PATH}/tools/bin2c.py" $< $@
