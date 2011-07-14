#Set This. It should be {/path/to/OE}/trunk/build/tmp/sysroots
export XBMCSTAGE=$(XBMC_DEPENDS)

export BUILD=$(shell uname -m)-linux
export BUILDPREFIX=${XBMCSTAGE}/usr
export PKG_CONFIG_SYSROOT_DIR=${XBMCSTAGE}
export CFLAGS=-isystem${BUILDPREFIX}/include -isystem$(DIRECTFB_DIR)/include -isystem$(DIRECTFB_DIR)/include/directfb \
       -isystem$(DCCHD_DIR)/../test/include_egl -isystem$(DCCHD_DIR)/core -isystem$(DCCHD_DIR)/mono
export CXXFLAGS=${CFLAGS}
export CPPFLAGS=${CFLAGS}
export LDFLAGS=-L${XBMCSTAGE}/lib -Wl,-rpath-link,${XBMCSTAGE}/lib \
       -L${BUILDPREFIX}/lib -Wl,-rpath-link,${BUILDPREFIX}/lib \
       -L$(DIRECTFB_DIR)/lib -Wl,-rpath-link,$(DIRECTFB_DIR)/lib \
       -L$(DIRECTFB_DIR)/install -Wl,-rpath-link,$(DIRECTFB_DIR)/install \
       -L$(EGL_DRIVERS_DIR)/usr/lib -Wl,-rpath-link,$(EGL_DRIVERS_DIR)/usr/lib \
       -L$(DCCHD_DIR)/dcchd -Wl,-rpath-link,$(DCCHD_DIR)/dcchd \
       -L$(DCCHD_DIR)/mono -Wl,-rpath-link,$(DCCHD_DIR)/mono \
       -L$(DCCHD_DIR)/core -Wl,-rpath-link,$(DCCHD_DIR)/core \
       -L$(DCCHD_DIR)/dtv -Wl,-rpath-link,$(DCCHD_DIR)/dtv \
       -L$(DCCHD_DIR)/dtv/network -Wl,-rpath-link,$(DCCHD_DIR)/dtv/network \
       -L$(DCCHD_DIR)/dtv/tuner -Wl,-rpath-link,$(DCCHD_DIR)/dtv/tuner \
       -L$(DCCHD_DIR)/dtv/capture -Wl,-rpath-link,$(DCCHD_DIR)/dtv/capture \
       -L$(DCCHD_DIR)/dtv/acap -Wl,-rpath-link,$(DCCHD_DIR)/dtv/acap \
       -L$(RUA_DIR)/lib -Wl,-rpath-link,$(RUA_DIR)/lib -Wl,-O1
#assume toolchain is in PATH
export CROSSBIN=mips-linux-gnu-
export CC=${CROSSBIN}gcc -march=mips32r2 -mtune=24kf -EL
export CXX=${CROSSBIN}g++ -march=mips32r2 -mtune=24kf -EL -fpermissive
export LD=${CROSSBIN}ld
export AR=${CROSSBIN}ar
export RANLIB=${CROSSBIN}ranlib
export STRIP=${CROSSBIN}strip
export OBJDUMP=${CROSSBIN}objdump
export HOST=mips-linux-gnu
export CXXCPP=${CXX} -E
export PKG_CONFIG_LIBDIR=${BUILDPREFIX}/lib/pkgconfig:$(DIRECTFB_DIR)/lib/pkgconfig
