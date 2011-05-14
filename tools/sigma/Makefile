include sigma-sdk.mk

SOURCE=../../
# configuration settings
export PKG_CONFIG_PATH=$(BUILDPREFIX)/lib/pkgconfig
CONFIGURE=./configure --host=$(HOST) --prefix=/usr --with-build-prefix=$(BUILDPREFIX) \
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

