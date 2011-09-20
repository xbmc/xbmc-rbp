include ../../Makefile.include
include ../depends.mk

# lib name, version
LIBNAME=SDL
VERSION=1.2.13
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.gz

# configuration settings
DIRECTFBCONFIG:=$(SDKSTAGE)/usr/local/bin/directfb-config
CONFIGURE= ./configure --prefix=$(PREFIX) --host=$(HOST) \
  --disable-audio --disable-cdrom --disable-video-x11 --disable-video-photon \
  --disable-video-cocoa --disable-video-fbcon --enable-video-directfb \
  --disable-video-ps2gs --disable-video-svga --disable-video-vgl \
  --disable-video-wscons --disable-video-xbios --disable-video-gem \
  --disable-osmesa-shared

LIBDYLIB=$(SOURCE)/$(LIBNAME)/.libs/$(LIBNAME).so

CLEAN_FILES=$(ARCHIVE) $(SOURCE)

all: $(LIBDYLIB) .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(SOURCE): $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	cd $(SOURCE); DIRECTFBCONFIG=${DIRECTFBCONFIG} $(CONFIGURE)

$(LIBDYLIB): $(SOURCE)
	make -j $(JOBS) -C $(SOURCE)

.installed:
	make -C $(SOURCE) install
	touch $@

clean:
	make -C $(SOURCE) clean
	rm -f .installed

distclean::
	rm -rf $(SOURCE) .installed

