COMPONENT_ADD_INCLUDEDIRS := include
COMPONENT_PRIV_INCLUDEDIRS := src heatshrink/src heatshrink/include
COMPONENT_SRCDIRS := src heatshrink/src
COMPONENT_OBJS := src/espfs.o src/espfs_vfs.o heatshrink/src/heatshrink_decoder.o
COMPONENT_EXTRA_CLEAN := mkespfsimage/*

IMAGEROOTDIR := $(subst ",,$(CONFIG_ESPFS_IMAGEROOTDIR))
MKESPFSIMAGE_BIN := mkespfsimage/mkespfsimage
FILES := $(shell find $(PROJECT_PATH)/$(IMAGEROOTDIR) | sed -E 's/([[:space:]])/\\\1/g')

COMPONENT_EXTRA_CLEAN += $(IMAGEROOTDIR)/*

ifeq ("$(CONFIG_ESPFS_LINK_BINARY)","y")
COMPONENT_OBJS += src/espfs_image.o
endif

ifeq ("$(CONFIG_ESPFS_USE_HEATSHRINK)","y")
USE_HEATSHRINK := "yes"
CFLAGS += -DESPFS_HEATSHRINK
COMPONENT_ADD_INCLUDEDIRS += lib/heatshrink
else
USE_HEATSHRINK := "no"
endif

ifeq ("$(CONFIG_ESPFS_USE_GZIP)","y")
USE_GZIP_COMPRESSION := "yes"
else
USE_GZIP_COMPRESSION := "no"
endif

npm_BINARIES :=
ifeq ("$(CONFIG_ESPFS_PREPROCESS_FILES)","y")
npm_PACKAGES :=
ifeq ("$(CONFIG_ESPFS_USE_BABEL)","y")
npm_BINARIES += bin/babel
npm_PACKAGES += @babel/core @babel/cli @babel/preset-env babel-preset-minify
bin/babel: node_modules
	mkdir -p bin
	ln -fs ../node_modules/.bin/babel bin/
endif

ifeq ("$(CONFIG_ESPFS_USE_HTMLMINIFIER)","y")
npm_BINARIES += bin/html-minifier
npm_PACKAGES += html-minifier
bin/html-minifier: node_modules
	mkdir -p bin
	ln -fs ../node_modules/.bin/html-minifier bin/
endif

ifeq ("$(CONFIG_ESPFS_USE_UGLIFYCSS)","y")
npm_BINARIES += bin/uglifycss
npm_PACKAGES += uglifycss
bin/uglifycss: node_modules
	mkdir -p bin
	ln -fs ../node_modules/.bin/uglifycss bin/
endif

ifeq ("$(CONFIG_ESPFS_USE_UGLIFYJS)","y")
npm_BINARIES += bin/uglifyjs
npm_PACKAGES += uglify-js
bin/uglifyjs: node_modules
	mkdir -p bin
	ln -fs ../node_modules/.bin/uglifyjs bin/
endif

node_modules:
	npm install --save-dev $(npm_PACKAGES)
endif

espfs_image.bin: $(FILES) $(npm_BINARIES) mkespfsimage/mkespfsimage
	BUILD_DIR="$(shell pwd)" \
	CONFIG_ESPFS_PREPROCESS_FILES=$(CONFIG_ESPFS_PREPROCESS_FILES) \
	CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS=$(CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS) \
	CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER=$(CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER) \
	CONFIG_ESPFS_JS_CONVERT_BABEL=$(CONFIG_ESPFS_JS_CONVERT_BABEL) \
	CONFIG_ESPFS_JS_MINIFY_BABEL=$(CONFIG_ESPFS_JS_MINIFY_BABEL) \
	CONFIG_ESPFS_JS_MINIFY_UGLIFYJS=$(CONFIG_ESPFS_JS_MINIFY_UGLIFYJS) \
	CONFIG_ESPFS_UGLIFYCSS_PATH=$(CONFIG_ESPFS_UGLIFYCSS_PATH) \
	CONFIG_ESPFS_HTMLMINIFIER_PATH=$(CONFIG_ESPFS_HTMLMINIFIER_PATH) \
	CONFIG_ESPFS_BABEL_PATH=$(CONFIG_ESPFS_BABEL_PATH) \
	CONFIG_ESPFS_UGLIFYJS_PATH=$(CONFIG_ESPFS_UGLIFYJS_PATH) \
	$(COMPONENT_PATH)/tools/build-image.py "$(PROJECT_PATH)/$(CONFIG_ESPFS_IMAGEROOTDIR)"

src/espfs_image.o: src/espfs_image.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

src/espfs_image.c: espfs_image.bin
	mkdir -p src
	xxd -i $< $@
	sed -i '1s;^;const __attribute__((aligned(4))); ' $@

mkespfsimage/mkespfsimage: $(COMPONENT_PATH)/mkespfsimage
	mkdir -p $(COMPONENT_BUILD_DIR)/mkespfsimage
	$(MAKE) -C $(COMPONENT_BUILD_DIR)/mkespfsimage -f $(COMPONENT_PATH)/mkespfsimage/Makefile \
		USE_HEATSHRINK="$(USE_HEATSHRINK)" USE_GZIP_COMPRESSION="$(USE_GZIP_COMPRESSION)" BUILD_DIR=$(COMPONENT_BUILD_DIR)/mkespfsimage \
		CC=$(HOSTCC) clean mkespfsimage
	mkdir -p bin
	if [ -f $(MKESPFSIMAGE_BIN) ]; then \
        ln -s ../$(MKESPFSIMAGE_BIN) bin/; \
    fi
	if [ -f $(MKESPFSIMAGE_BIN).exe ]; then \
        ln -s ../$(MKESPFSIMAGE_BIN).exe bin/; \
    fi
