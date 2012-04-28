include ../../Makefile.include
include ../depends.mk

LIBNAME=boost
VERSION=1_44_0
SOURCE=$(LIBNAME)_$(VERSION)
ARCHIVE=$(SOURCE).tar.bz2

export PATH:=$(TOOLCHAIN)/bin:$(PATH)

all: .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

.installed: $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	cat user-config.jam-arm.in > $(SOURCE)/tools/build/v2/user-config.jam
	sed -e 's:arm-unknown-linux-gnueabi:$(HOST):g' -i $(SOURCE)/tools/build/v2/user-config.jam
	cd $(SOURCE); patch -p1 < ../add-arm-mem-barrier.patch
	cd $(SOURCE); patch -p1 < ../fix-deprecated-swp.patch
	cd $(SOURCE); ./bootstrap.sh --prefix=$(PREFIX) --with-libraries=thread
	cd $(SOURCE); ./bjam --prefix=$(PREFIX) toolset=gcc --toolset-root=$(TOOLCHAIN) --architecture=arm install
	touch .installed

clean:
	rm -rf $(SOURCE) .installed

distclean::
	rm -rf $(SOURCE) .installed


