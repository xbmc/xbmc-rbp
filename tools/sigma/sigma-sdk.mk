#Set This. It should be {/path/to/OE}/trunk/build/tmp/sysroots/
export XBMCSTAGE=

export BUILD=$(shell uname -m)-linux
export PREFIX=${XBMCSTAGE}/mips-linux-gnu/usr
export PKG_CONFIG_SYSROOT_DIR=${XBMCSTAGE}/mips-linux-gnu
export CFLAGS=-isystem${PREFIX}/include
export CXXFLAGS=${CFLAGS}
export CPPFLAGS=${CFLAGS}
export LDFLAGS=-L${XBMCSTAGE}/mips-linux-gnu/lib -Wl,-rpath-link,${XBMCSTAGE}/mips-linux-gnu/lib -L${PREFIX}/lib -Wl,-rpath-link,${PREFIX}/lib -Wl,-O1
export CROSSBIN=${XBMCSTAGE}/$(BUILD)/usr/bin/mips-linux-gnu-
export CC=${CROSSBIN}gcc -march=mips32r2 -mtune=24kf -EL
export CXX=${CROSSBIN}g++ -march=mips32r2 -mtune=24kf -EL -fpermissive
export LD=${CROSSBIN}ld
export AR=${CROSSBIN}ar
export RANLIB=${CROSSBIN}ranlib
export STRIP=${CROSSBIN}strip
export OBJDUMP=${CROSSBIN}objdump
export ACLOCAL=aclocal -I ${PREFIX}/share/aclocal -I ${PREFIX}/share/aclocal-1.11
export HOST=mips-linux-gnu
export CXXCPP=${CXX} -E
export PKG_CONFIG_LIBDIR=${PREFIX}/lib/pkgconfig
export TARGETFS=${XBMCSTAGE}/targetfs
export PATH:=${XBMCSTAGE}/${BUILD}/usr/mips/bin:${XBMCSTAGE}/${BUILD}/bin:${XBMCSTAGE}/${BUILD}/usr/bin:$(PATH)
