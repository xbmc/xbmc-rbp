#Set This. It should be {/path/to/OE}/trunk/build/tmp/sysroots/
export XBMCSTAGE=/home/davilla/xbmc-sigma/trunk/build/tmp/sysroots

export HOST=mips-linux-gnu
export BUILD=i686-linux
export PREFIX=${XBMCSTAGE}/mips-linux-gnu/usr
export PKG_CONFIG_SYSROOT_DIR=${XBMCSTAGE}/mips-linux-gnu
export CFLAGS=-isystem${PREFIX}/include
export CXXFLAGS=${CFLAGS}
export CPPFLAGS=${CFLAGS}
export LDFLAGS=-L${XBMCSTAGE}/mips-linux-gnu/lib -Wl,-rpath-link,${XBMCSTAGE}/mips-linux-gnu/lib -L${PREFIX}/lib -Wl,-rpath-link,${PREFIX}/lib -Wl,-O1
export CROSSBIN=${XBMCSTAGE}/${BUILD}/usr/bin/mips-linux-gnu-
export CC=${CROSSBIN}gcc -march=mips32r2 -mtune=24kf -EL
export CXX=${CROSSBIN}g++ -march=mips32r2 -mtune=24kf -EL -fpermissive
export LD=${CROSSBIN}ld
export AR=${CROSSBIN}ar
export RANLIB=${CROSSBIN}ranlib
export STRIP=${CROSSBIN}strip
export OBJDUMP=${CROSSBIN}objdump
export ACLOCAL=aclocal -I ${PREFIX}/share/aclocal -I ${PREFIX}/share/aclocal-1.11
export CXXCPP=${CXX} -E
export PKG_CONFIG_LIBDIR=${PREFIX}/lib/pkgconfig
export TARGETFS=${XBMCSTAGE}/targetfs
export PATH:=${XBMCSTAGE}/${BUILD}/usr/mips/bin:${XBMCSTAGE}/${BUILD}/bin:${XBMCSTAGE}/${BUILD}/usr/bin:$(PATH)

export DCCHD_DIR=/home/davilla/xbmc-sigma/trunk/build/tmp/work/smp8656-linux-gnu/dcchd-smp865x-black-3.10.0-r1/dcchd_SMP865x_3_10_0_black.mips/dcchd
export RUA_DIR=/home/davilla/xbmc-sigma/trunk/build/tmp/work/smp8656-linux-gnu/mrua-smp8656f-3.10.0-r1/mrua_SMP8656F_3_10_0_dev.mips
export RMCFLAGS=-DEM86XX_CHIP=EM86XX_CHIPID_TANGO3 -DEM86XX_REVISION=3 -DXBOOT2_SMP865X=1 -DEM86XX_MODE=EM86XX_MODEID_STANDALONE -DWITH_XLOADED_UCODE=1 -DXBOOT2_SMP8652=1 -DXBOOT2_SMP8656=1 -DWITHOUT_RMOUTPUT=1
