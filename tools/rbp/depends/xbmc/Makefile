include ../../Makefile.include
include ../depends.mk

SOURCE=../../../../
# configuration settings
export SDKSTAGE
# turn off 'the mangling of 'va_list' has changed in GCC 4.4' warnings
export CFLAGS+=-Wno-psabi

export PYTHON_VERSION=2.7
export PYTHON_LDFLAGS=-L$(SDKSTAGE)/usr/lib -lpython$(PYTHON_VERSION) -lpthread -ldl -lutil -lm
export PYTHON_CPPFLAGS=-I$(SDKSTAGE)/usr/include/python$(PYTHON_VERSION)
export PYTHON_SITE_PKG=$(SDKSTAGE)/usr/lib/python$(PYTHON_VERSION)/site-packages
export PYTHON_NOVERSIONCHECK=no-check
export USE_TEXTUREPACKER_NATIVE_ROOT=$(BUILDROOT)/output/host

export PATH:=$(SYSROOT)/usr/bin:$(PATH)
CONFIGURE=./configure --prefix=$(PREFIX) --build=$(BUILD) --host=$(HOST) \
  --enable-gles --enable-external-python --disable-sdl --disable-x11 --disable-xrandr \
  --disable-optical-drive --disable-dvdcss --disable-joystick --disable-debug \
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
