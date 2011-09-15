include ../../Makefile.include
include ../depends.mk

LIBNAME=mysql
VERSION=5.1.55
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.gz

# configuration settings
CONFIGURE=./configure --prefix=$(PREFIX) --host=$(HOST) --enable-shared \
  --enable-thread-safe-client -with-extra-charsets=complex \
  --with-readline=$(PREFIX) --without-readline \
  --without-server --without-docs --without-man \
  ac_cv_sys_restartable_syscalls=yes \
  ac_cv_path_PS=/bin/ps \
  ac_cv_FIND_PROC="/bin/ps p \$\$PID | grep -v grep | grep mysqld > /dev/null" \
  ac_cv_have_decl_HAVE_IB_ATOMIC_PTHREAD_T_GCC=yes \
  ac_cv_have_decl_HAVE_IB_ATOMIC_PTHREAD_T_SOLARIS=no \
  ac_cv_have_decl_HAVE_IB_GCC_ATOMIC_BUILTINS=yes \

  tmp =mysql_cv_new_rl_interface=yes \


LIBDYLIB=$(SOURCE)/libmysql/.libs/$(LIBNAME).so

CLEAN_FILES=$(ARCHIVE) $(SOURCE)

all: $(LIBDYLIB) .installed

$(TARBALLS_LOCATION)/$(ARCHIVE):
	$(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(SOURCE): $(TARBALLS_LOCATION)/$(ARCHIVE)
	-rm -rf $(SOURCE)
	$(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	echo $(SOURCE) > .gitignore
	#cd $(SOURCE); patch -p1 <../use-new-readline-iface.patch
	cd $(SOURCE); patch -p1 <../01-mysqlclient-cross-compile.patch
	cd $(SOURCE); patch -p1 <../02-mysqlclient-ios.patch
	cd $(SOURCE); autoreconf -vif;
	cd $(SOURCE); $(CONFIGURE)

$(LIBDYLIB): $(SOURCE)
	make -j $(JOBS) -C $(SOURCE)

.installed:
	make -C $(SOURCE)/scripts install
	make -C $(SOURCE)/include install
	make -C $(SOURCE)/libmysql install
	touch $@

clean:
	make -C $(SOURCE) clean
	rm -f .installed

distclean::
	rm -rf $(SOURCE) .installed

