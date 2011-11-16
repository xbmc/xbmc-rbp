export HOST=arm-bcm2708-linux-gnueabi
export BUILD=i686-linux
export PREFIX=${XBMCPREFIX}
export TARGETFS
export SYSROOT=/usr/local/bcm-gcc/arm-bcm2708-linux-gnueabi/sys-root
export RLINK_PATH=-Wl,-rpath-link,${SYSROOT}/lib -Wl,-rpath-link,${TARGETFS}/lib -Wl,-rpath-link,${TARGETFS}/usr/lib -Wl,-rpath-link,${TARGETFS}/opt/vc/lib

export CFLAGS=-isystem${XBMCPREFIX}/include -isystem${SDKSTAGE}/usr/include -isystem${SDKSTAGE}/opt/vc/include -isystem${SDKSTAGE}/opt/vc
export CFLAGS+=-L${XBMCPREFIX}/lib -L${SYSROOT}/lib -L${TARGETFS}/lib -L${TARGETFS}/usr/lib -L${TARGETFS}/opt/vc/lib ${RLINK_PATH}
export CXXFLAGS=${CFLAGS}
export CPPFLAGS=${CFLAGS}
export LDFLAGS=${RLINK_PATH} -L${TARGETFS}/lib -L${TARGETFS}/usr/lib -L${XBMCPREFIX}/lib
export LD=${TOOLCHAIN}/bin/${HOST}-ld
export AR=${TOOLCHAIN}/bin/${HOST}-ar
export CC=${TOOLCHAIN}/bin/${HOST}-gcc
export CXX=${TOOLCHAIN}/bin/${HOST}-g++
export CXXCPP=${CXX} -E
export RANLIB=${TOOLCHAIN}/bin/${HOST}-ranlib
export STRIP=${TOOLCHAIN}/bin/${HOST}-strip
export OBJDUMP=${TOOLCHAIN}/bin/${HOST}-objdump 
export ACLOCAL=aclocal -I ${SDKSTAGE}/usr/share/aclocal -I ${TARGETFS}/usr/share/aclocal-1.11
export PKG_CONFIG_LIBDIR=${PREFIX}/lib/pkgconfig:${SDKSTAGE}/lib/pkgconfig:${SDKSTAGE}/usr/lib/pkgconfig
export PATH:=${PREFIX}/bin:$(PATH):${TOOLCHAIN}/bin
