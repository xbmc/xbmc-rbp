include ../../Makefile.include
include ../depends.mk

SOURCE=../../../../
# configuration settings
export SDKSTAGE
# turn off 'the mangling of 'va_list' has changed in GCC 4.4' warnings
export CFLAGS+=-Wno-psabi
export ALSA_CFLAGS=-I$(SDKSTAGE)/usr/include/alsa
export PKG_CONFIG_PATH=$(PREFIX)/bin/pkg-config
export PYTHON_VERSION=2.6
export PYTHON_LDFLAGS=-L$(SDKSTAGE)/usr/lib -lpython2.6
export PYTHON_CPPFLAGS=-I$(SDKSTAGE)/usr/include/python2.6
export PYTHON_SITE_PKG=$(SDKSTAGE)/usr/lib/python2.6/site-packages
export PYTHON_NOVERSIONCHECK=no-check
export USE_TEXTUREPACKER_NATIVE_ROOT=/usr
CONFIGURE=./configure --prefix=$(PREFIX) --build=$(BUILD) --host=$(HOST) \
  --enable-gles --disable-sdl --disable-x11 --disable-xrandr \
  --disable-optical-drive --disable-dvdcss --disable-joystick

all: configure

clean:
distclean:
	cd $(SOURCE);

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)
	#cd $(SOURCE); make -j $(JOBS)
	#cd $(SOURCE); make install
