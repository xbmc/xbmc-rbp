export HOST=arm-unknown-linux-gnueabi
export BUILD=i686-linux
export PREFIX=$(XBMCPREFIX)
export SYSROOT=$(BUILDROOT)/output/host/usr/arm-unknown-linux-gnueabi/sysroot
export CFLAGS=-pipe -O3 -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -mfloat-abi=softfp -mfpu=vfp -O3 -mabi=aapcs-linux -Wno-psabi -Wa,-mno-warn-deprecated -isystem$(SYSROOT)/usr/include -isystem$(SYSROOT)/opt/vc/include -isystem$(PREFIX)/include -isystem$(PREFIX)/usr/include/mysql
export CXXFLAGS=$(CFLAGS)
export CPPFLAGS=$(CFLAGS)
export LDFLAGS=-L$(SYSROOT)/opt/vc/lib -L$(XBMCPREFIX)/lib
export LD=$(TOOLCHAIN)/bin/$(HOST)-ld --sysroot=$(SYSROOT)
export CC=$(TOOLCHAIN)/bin/$(HOST)-gcc --sysroot=$(SYSROOT)
export CXX=$(TOOLCHAIN)/bin/$(HOST)-g++ --sysroot=$(SYSROOT)
export OBJDUMP=$(TOOLCHAIN)/bin/$(HOST)-objdump
export RANLIB=$(TOOLCHAIN)/bin/$(HOST)-ranlib
export STRIP=$(TOOLCHAIN)/bin/$(HOST)-strip
export AR=$(TOOLCHAIN)/bin/$(HOST)-ar
export CXXCPP=$(CXX) -E
export PKG_CONFIG_PATH=$(PREFIX)/lib/pkgconfig
export PATH:=$(PREFIX)/bin:$(BUILDROOT)/output/host/usr/bin:$(PATH)
