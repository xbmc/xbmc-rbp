include sigma-sdk.mk

SOURCE=../../
# configuration settings
export PKG_CONFIG_PATH=$(PREFIX)/lib/pkgconfig
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --enable-external-python --enable-gles --disable-xrandr \
  --disable-optical-drive --disable-dvdcss --enable-optimizations \
  --disable-x11 --disable-webserver --disable-joystick --disable-libbluray \
  --enable-sigmasmp
all: configure

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)
	cd $(SOURCE); make
	#cd $(SOURCE); make install

