#!/bin/bash

SCRIPT_PATH=$(cd `dirname $0` && pwd)

#Edit these two
BUILDROOT=/opt/xbmc-bcm/buildroot
TARBALLS=/opt/xbmc-tarballs

XBMCPREFIX=/opt/xbmc-bcm/xbmc-bin
SDKSTAGE=$BUILDROOT/output/staging
TARGETFS=$BUILDROOT/output/target
#TOOLCHAIN=$BUILDROOT/output/host/opt/ext-toolchain/
TOOLCHAIN=$BUILDROOT/output/host/usr/
#
sudo mkdir -p $XBMCPREFIX
sudo chmod 777 $XBMCPREFIX
mkdir -p $XBMCPREFIX/lib
mkdir -p $XBMCPREFIX/include

echo "SDKSTAGE=$SDKSTAGE"                                              >  $SCRIPT_PATH/Makefile.include
echo "XBMCPREFIX=$XBMCPREFIX"                                          >> $SCRIPT_PATH/Makefile.include
echo "TARGETFS=$TARGETFS"                                              >> $SCRIPT_PATH/Makefile.include
echo "TOOLCHAIN=$TOOLCHAIN"                                            >> $SCRIPT_PATH/Makefile.include
echo "BUILDROOT=$BUILDROOT"                                            >> $SCRIPT_PATH/Makefile.include
echo "BASE_URL=http://mirrors.xbmc.org/build-deps/darwin-libs"         >> $SCRIPT_PATH/Makefile.include
echo "TARBALLS_LOCATION=$TARBALLS"                                     >> $SCRIPT_PATH/Makefile.include
echo "RETRIEVE_TOOL=/usr/bin/curl"                                     >> $SCRIPT_PATH/Makefile.include
echo "RETRIEVE_TOOL_FLAGS=-Ls --create-dirs --output \$(TARBALLS_LOCATION)/\$(ARCHIVE)" >> $SCRIPT_PATH/Makefile.include
echo "ARCHIVE_TOOL=/bin/tar"                                           >> $SCRIPT_PATH/Makefile.include
echo "ARCHIVE_TOOL_FLAGS=xf"                                           >> $SCRIPT_PATH/Makefile.include
echo "JOBS=$((`grep -c processor /proc/cpuinfo` -1))"                  >> $SCRIPT_PATH/Makefile.include
