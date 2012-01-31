include ../../Makefile.include
include ../depends.mk

SOURCE=../../../../

ifeq ($(USE_BUILDROOT),1)
	export PATH:=$(PREFIX)/bin:$(BUILDROOT)/output/host/usr/bin:$(SYSROOT)/usr/bin:$(PATH)
endif

CONFIGURE=./configure --prefix=$(PREFIX) --build=$(BUILD) --host=$(HOST) \
	--enable-gles --enable-external-python --disable-sdl --disable-x11 --disable-xrandr \
	--disable-optical-drive --disable-dvdcss --disable-joystick --disable-debug \
	--disable-crystalhd --disable-vda --disable-vtbdecoder --disable-vaapi --disable-vdpau \
	--enable-platform-raspberry-pi --disable-openmax --disable-pulse --disable-projectm

all: configure

clean:
distclean:
	cd $(SOURCE);

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)
	#cd $(SOURCE); make -j $(JOBS)
	#cd $(SOURCE); make install
