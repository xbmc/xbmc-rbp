include ../../Makefile.include
include ../depends.mk

# lib name, version
LIBNAME=samba
VERSION=3.0.37
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.gz

# configuration settings
# disable python support
export PYTHON_VER=0.0
export OPT="${CFLAGS}"
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --disable-cups --enable-static --disable-shared --disable-pie --disable-fam \
  --disable-shared-libs --without-ldap --without-ads -disable-iprint  \
  --without-automount --without-winbind --without-sendfile-support \
  --with-included-popt --with-included-iniparser --without-sys-quotas --without-krb5 --without-swat \
  SMB_BUILD_CC_NEGATIVE_ENUM_VALUES=yes \
  libreplace_cv_READDIR_GETDIRENTRIES=no \
  libreplace_cv_READDIR_GETDENTS=no \
  linux_getgrouplist_ok=no \
  samba_cv_REPLACE_READDIR=no \
  samba_cv_HAVE_WRFILE_KEYTAB=yes \
  samba_cv_HAVE_KERNEL_OPLOCKS_LINUX=yes \
  samba_cv_HAVE_IFACE_IFCONF=yes \
  ac_cv_header_libunwind_h=no \
  ac_cv_header_execinfo_h=no \
  samba_cv_SYSCONF_SC_NPROCESSORS_ONLN=no \

LIBDYLIB=$(SOURCE)/source/bin/libsmbclient.a

CLEAN_FILES=$(ARCHIVE) $(SOURCE)

all: $(LIBDYLIB) .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(LIBDYLIB): $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	cd $(SOURCE); patch -p1 < ../wle-fix.patch
	cd $(SOURCE); patch -p1 < ../silence-receive-warning.patch
	cd $(SOURCE); patch -p1 < ../samba-getgrouplist.patch
	cd $(SOURCE)/source; ./autogen.sh
	cd $(SOURCE)/source; $(CONFIGURE)
	cd $(SOURCE)/source; mkdir bin
	cd $(SOURCE)/source; make -j 1 bin/libsmbclient.a

.installed:
	cd $(SOURCE)/source; make installclientlib
	touch $@

clean:
	make -C $(SOURCE)/source clean
	rm -f .installed

distclean::
	rm -rf $(SOURCE) .installed

