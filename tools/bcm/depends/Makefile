include ../Makefile.include

BUILDTOOLS = help2man autoconf automake libtool pkg-config yasm cmake patchelf

SUBDIRS = \
	pcre expat gettext readline sqlite3 libgpg-error libgcrypt \
	bzip2 liblzo2 libxml2 freetype2 fontconfig \
	openssl libssh2 curl \
	libpng libjpeg tiff jasper \
	libogg libvorbis libflac libmad fribidi libwavpack libmpeg2 \
	libass libcdio libsamplerate \
	libmicrohttpd libmodplug librtmp libbluray yajl \
	samba dbus python26 mysqlclient boost xbmc \

IMAGENAME = xbmc-aml-`date +'%y%m%d'`

.PHONY: $(BUILDTOOLS) $(SUBDIRS)

all: $(BUILDTOOLS) $(SUBDIRS)

$(BUILDTOOLS):
	$(MAKE) -C $@

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for d in $(BUILDTOOLS); do $(MAKE) -C $$d clean; done
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

distclean::
	for d in $(BUILDTOOLS); do $(MAKE) -C $$d distclean; done
	for d in $(SUBDIRS); do $(MAKE) -C $$d distclean; done
	rm -rf $(XBMCPREFIX)

image:
	make -C $(BUILDROOT) tarroot IMAGE=$(CURDIR)/$(IMAGENAME) TAR_COMPRESSOR=
	echo "S111xbmc S110rc.local" | cat $(TARGETFS)/etc/init.d/rc.deps - > $(CURDIR)/packaging/etc/init.d/rc.deps
	cd packaging; tar -uf $(CURDIR)/$(IMAGENAME).tar *
	tar -uf $(CURDIR)/$(IMAGENAME).tar $(XBMCPREFIX) 
	gzip $(IMAGENAME).tar
