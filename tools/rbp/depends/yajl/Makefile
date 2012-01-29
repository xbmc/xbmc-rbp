include ../../Makefile.include
include ../depends.mk

# lib name, version
LIBNAME=yajl
VERSION=2.0.1
SOURCE=lloyd-$(LIBNAME)-f4b2b1a
ARCHIVE=$(LIBNAME)-$(VERSION).tar.gz
BASE_URL=http://github.com/lloyd/yajl/tarball/2.0.1
CMAKE=cmake
# configuration settings
LIBDYLIB=$(SOURCE)/build/$(LIBNAME)-$(VERSION)/lib/libyajl.so

CLEAN_FILES=$(ARCHIVE) $(SOURCE)

all: $(LIBDYLIB) .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)

$(SOURCE): $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	cd $(SOURCE); mkdir build; cd build; $(CMAKE) -DCMAKE_INSTALL_PREFIX='$(PREFIX)' ../

$(LIBDYLIB): $(SOURCE)
	make -j $(JOBS) -C $(SOURCE)/build

.installed:
	make -C $(SOURCE)/build install
	touch $@

clean:
	make -C $(SOURCE)/build clean
	rm -f .installed

distclean::
	rm -rf $(SOURCE) .installed


