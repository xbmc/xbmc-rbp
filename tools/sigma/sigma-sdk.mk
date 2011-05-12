#Set This. It should be {/path/to/OE}/trunk/build
export OEROOT=
export RUA_DIR=${OEROOT}/tmp/work/smp8656-linux-gnu/mrua-smp8656f-3.10.0-r1/mrua_SMP8656F_3_10_0_dev.mips
export DCCHD_DIR=${OEROOT}/tmp/work/smp8656-linux-gnu/dcchd-smp865x-black-3.10.0-r1/dcchd_SMP865x_3_10_0_black.mips/dcchd
export XBMCSTAGE=${OEROOT}/tmp/sysroots

export BUILD=$(shell uname -m)-linux
export BUILDPREFIX=${XBMCSTAGE}/mips-linux-gnu/usr
export PKG_CONFIG_SYSROOT_DIR=${XBMCSTAGE}/mips-linux-gnu
export CFLAGS=-isystem${BUILDPREFIX}/include
export CXXFLAGS=${CFLAGS}
export CPPFLAGS=${CFLAGS}
export LDFLAGS=-L${XBMCSTAGE}/mips-linux-gnu/lib -Wl,-rpath-link,${XBMCSTAGE}/mips-linux-gnu/lib -L${BUILDPREFIX}/lib -Wl,-rpath-link,${BUILDPREFIX}/lib -Wl,-O1
export CROSSBIN=${XBMCSTAGE}/$(BUILD)/usr/bin/mips-linux-gnu-
export CC=${CROSSBIN}gcc -march=mips32r2 -mtune=24kf -EL
export CXX=${CROSSBIN}g++ -march=mips32r2 -mtune=24kf -EL -fpermissive
export LD=${CROSSBIN}ld
export AR=${CROSSBIN}ar
export RANLIB=${CROSSBIN}ranlib
export STRIP=${CROSSBIN}strip
export OBJDUMP=${CROSSBIN}objdump
export ACLOCAL=aclocal -I ${BUILDPREFIX}/share/aclocal -I ${BUILDPREFIX}/share/aclocal-1.11
export HOST=mips-linux-gnu
export CXXCPP=${CXX} -E
export PKG_CONFIG_LIBDIR=${BUILDPREFIX}/lib/pkgconfig
export TARGETFS=${XBMCSTAGE}/targetfs
export PATH:=${XBMCSTAGE}/${BUILD}/usr/mips/bin:${XBMCSTAGE}/${BUILD}/bin:${XBMCSTAGE}/${BUILD}/usr/bin:$(PATH)
