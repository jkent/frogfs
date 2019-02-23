COMPONENT_ADD_INCLUDEDIRS := include
COMPONENT_PRIV_INCLUDEDIRS := . heatshrink/src heatshrink/include
COMPONENT_ADD_LDFLAGS += -limage-espfs
COMPONENT_SRCDIRS := . heatshrink/src
COMPONENT_OBJS := espfs.o esp_espfs.o heatshrink/src/heatshrink_decoder.o
COMPONENT_EXTRA_CLEAN := mkespfsimage/*

IMAGEROOTDIR := $(subst ",,$(CONFIG_ESPFS_IMAGEROOTDIR))
FILES := $(shell find $(PROJECT_PATH)/$(IMAGEROOTDIR) | sed -E 's/([[:space:]])/\\\1/g')

COMPONENT_EXTRA_CLEAN += $(IMAGEROOTDIR)/*

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

IMAGE_PREREQ :=
ifeq ("$(CONFIG_ESPFS_USE_MINIFY_TOOLS)","y")
COMPONENT_EXTRA_CLEAN += $(IMAGEROOTDIR)/*
BABEL ?= $(CONFIG_ESPFS_BABEL_PATH)
HTMLMINIFIER ?= $(CONFIG_ESPFS_HTMLMINIFIER_PATH)
UGLIFYJS ?= $(CONFIG_ESPFS_UGLIFYJS_PATH)
UGLIFYCSS ?= $(CONFIG_ESPFS_UGLIFYCSS_PATH)
ifeq ("$(CONFIG_ESPFS_FETCH_MINIFY_TOOLS)","y")
COMPONENT_EXTRA_CLEAN += node_modules/*
BABEL := node_modules/.bin/babel
HTMLMINIFIER := node_modules/.bin/html-minifier
UGLIFYJS := node_modules/.bin/uglifyjs
UGLIFYCSS := node_modules/.bin/uglifycss
IMAGE_PREREQ += $(BABEL) $(HTMLMINIFIER) $(UGLIFYJS) $(UGLIFYCSS)
endif
endif

libespfs.a: libimage-espfs.a

image.espfs: $(FILES) $(IMAGE_PREREQ) mkespfsimage/mkespfsimage
ifeq ("$(CONFIG_ESPFS_USE_MINIFY_TOOLS)","y")
	rm -rf $(IMAGEROOTDIR)
	mkdir -p $(IMAGEROOTDIR)
	cp -r $(PROJECT_PATH)/$(IMAGEROOTDIR)/. $(IMAGEROOTDIR)
	files=$$(find $(IMAGEROOTDIR) -type f \( -name \*.css -o -name \*.html -o -name \*.js \)); \
	for file in $$files; do \
		case "$$file" in \
		*.min.css|*.min.js) continue ;; \
		*.css) \
			$(UGLIFYCSS) "$$file" > "$${file}.new"; \
			mv "$${file}.new" "$$file";; \
		*.html) \
			$(HTMLMINIFIER) --collapse-whitespace --remove-comments --use-short-doctype --minify-css true --minify-js true "$$file" > "$${file}.new"; \
			mv "$${file}.new" "$$file";; \
		*.js) \
			$(BABEL) --presets env "$$file" | $(UGLIFYJS) > "$${file}.new"; \
			mv "$${file}.new" "$$file";; \
		esac; \
	done
	awk "BEGIN {printf \"minify ratio was: %.2f%%\\n\", (`du -b -s $(IMAGEROOTDIR)/ | sed 's/\([0-9]*\).*/\1/'`/`du -b -s $(PROJECT_PATH)/$(IMAGEROOTDIR) | sed 's/\([0-9]*\).*/\1/'`)*100}"
	cd $(IMAGEROOTDIR); find . | $(COMPONENT_BUILD_DIR)/mkespfsimage/mkespfsimage > $(COMPONENT_BUILD_DIR)/image.espfs
else
	cd $(PROJECT_PATH)/$(IMAGEROOTDIR) && find . | $(COMPONENT_BUILD_DIR)/mkespfsimage/mkespfsimage > $(COMPONENT_BUILD_DIR)/image.espfs
endif

libimage-espfs.a: image.espfs
	$(OBJCOPY) -I binary -O elf32-xtensa-le -B xtensa --rename-section .data=.rodata \
		image.espfs image.espfs.o.tmp
	$(CC) -nostdlib -Wl,-r image.espfs.o.tmp -o image.espfs.o -Wl,-T $(COMPONENT_PATH)/image.espfs.ld
	$(AR) cru $@ image.espfs.o

mkespfsimage/mkespfsimage: $(COMPONENT_PATH)/mkespfsimage
	mkdir -p $(COMPONENT_BUILD_DIR)/mkespfsimage
	$(MAKE) -C $(COMPONENT_BUILD_DIR)/mkespfsimage -f $(COMPONENT_PATH)/mkespfsimage/Makefile \
		USE_HEATSHRINK="$(USE_HEATSHRINK)" USE_GZIP_COMPRESSION="$(USE_GZIP_COMPRESSION)" BUILD_DIR=$(COMPONENT_BUILD_DIR)/mkespfsimage \
		CC=$(HOSTCC) clean mkespfsimage

node_modules/.bin/babel:
	npm install --save-dev babel-cli babel-preset-env

node_modules/.bin/html-minifier:
	npm install --save-dev html-minifier

node_modules/.bin/uglifycss:
	npm install --save-dev uglifycss

node_modules/.bin/uglifyjs:
	npm install --save-dev uglify-js
