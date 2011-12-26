include ../../Makefile.include
include ../depends.mk

# lib name, version
LIBNAME=samba
VERSION=3.6.0
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.gz

# configuration settings
# disable python support
export PYTHON_VER=0.0
export OPT="${CFLAGS}"
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) \
  --disable-cups --disable-static --enable-shared --disable-pie --disable-relro \
	--disable-dnssd --disable-fam --disable-swat \
	--disable-shared-libs --without-ldap --without-ads -disable-iprint \
  --without-automount --without-winbind --without-sendfile-support \
	--without-cluster-support --without-cifsupcall --with-included-popt \
	--with-included-iniparser --without-sys-quotas --without-krb5 --without-swat \
	--enable-external-{libtdb,libtalloc} \
	samba_cv_HAVE_GETTIMEOFDAY_TZ=yes \
  samba_cv_USE_SETREUID=yes \
  samba_cv_HAVE_KERNEL_OPLOCKS_LINUX=yes \
  samba_cv_HAVE_IFACE_IFCONF=yes \
  samba_cv_HAVE_MMAP=yes \
  samba_cv_HAVE_FCNTL_LOCK=yes \
  samba_cv_HAVE_SECURE_MKSTEMP=yes \
  samba_cv_CC_NEGATIVE_ENUM_VALUES=yes \
  samba_cv_fpie=no \
	samba_cv_CC_NEGATIVE_ENUM_VALUES=yes \
	libreplace_cv_HAVE_GETADDRINFO=no \
	libreplace_cv_HAVE_IPV6=no \
	ac_cv_file__proc_sys_kernel_core_pattern=yes \

LIBDYLIB=$(SOURCE)/source/bin/libsmbclient.a

CLEAN_FILES=$(ARCHIVE) $(SOURCE)

all: $(LIBDYLIB) .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(LIBDYLIB): $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	#cd $(SOURCE); patch -p1 < ../wle-fix.patch
	#cd $(SOURCE); patch -p1 < ../silence-receive-warning.patch
	#cd $(SOURCE); patch -p1 < ../samba-getgrouplist.patch
	cd $(SOURCE); patch -p1 < ../01-bugfix-8385.patch
	#cd $(SOURCE); patch -p1 < ../100_configure_fixes.patch
	#cd $(SOURCE)/source3; ./autogen.sh
	cd $(SOURCE)/source3; $(CONFIGURE)
	cd $(SOURCE)/source3; mkdir bin
	cd $(SOURCE)/source3; make -j 1 bin/libsmbclient.a

.installed:
	cd $(SOURCE)/source3; make installclientlib
	touch $@

clean:
	make -C $(SOURCE)/source3 clean
	rm -f .installed

distclean::
	rm -rf $(SOURCE) .installed

