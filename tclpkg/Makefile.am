## Process this file with automake to produce Makefile.in

pkgsharpdir = $(pkglibdir)/sharp
pkggodir = $(pkglibdir)/go
pkgguiledir = $(pkglibdir)/guile
pkgiodir = $(pkglibdir)/io
pkgjavadir = $(pkglibdir)/java
pkgluadir = $(pkglibdir)/lua
pkgperldir = $(pkglibdir)/perl
pkgphpdir = $(pkglibdir)/php
pkgpython3dir = $(pkglibdir)/python3
pkgRdir = $(pkglibdir)/R
pkgrubydir = $(pkglibdir)/ruby
pkgtcldir = $(pkglibdir)/tcl

pkgindexdir = $(pkgtcldir)
if WITH_TCL
pkgindex_DATA = pkgIndex.tcl
endif

SUBDIRS = tclstubs gdtclft tcldot tclpathplan gv

pkgIndex.tcl: $(TCL_PKGINDEX) $(TCL_PKGINDEX_GD) $(TCL_PKGINDEX_SWIG)
	cat $(TCL_PKGINDEX) $(TCL_PKGINDEX_GD) $(TCL_PKGINDEX_SWIG) >$@
	echo "# end" >>$@

if ENABLE_SHARED
# Installation of softlinks into non-owned directories is done as a
# separate step, with soft failures, so that a developer can do:
#    ./configure --prefix=$HOME/graphviz; make; make install
# without root privileges.
install-data-hook:
if WITH_LUA
	-mkdir -p $(DESTDIR)$(LUA_INSTALL_DIR);
	if test -w $(DESTDIR)$(LUA_INSTALL_DIR); then \
		(cd $(DESTDIR)$(LUA_INSTALL_DIR); \
			cp -f $(DESTDIR)$(pkgluadir)/libgv_lua.so gv.so;) \
	else \
		echo "Warning: $(LUA_INSTALL_DIR) is not writable."; \
		echo "Skipping system installation of lua binding."; \
	fi
endif
if WITH_PERL
	-mkdir -p $(DESTDIR)$(PERL_INSTALL_DIR);
	if test -w $(DESTDIR)$(PERL_INSTALL_DIR); then \
		(cd $(DESTDIR)$(PERL_INSTALL_DIR); \
			cp -f $(DESTDIR)$(pkgperldir)/libgv_perl.so gv.so; \
			cp -f $(DESTDIR)$(pkgperldir)/gv.pm gv.pm;) \
	else \
		echo "Warning: $(PERL_INSTALL_DIR) is not writable."; \
		echo "Skipping system installation of perl binding."; \
	fi
endif
if WITH_PHP
	-mkdir -p $(DESTDIR)$(PHP_INSTALL_DIR);
	if test -w $(DESTDIR)$(PHP_INSTALL_DIR); then \
		(cd $(DESTDIR)$(PHP_INSTALL_DIR); \
			cp -f $(DESTDIR)$(pkgphpdir)/libgv_php.so gv.so;) \
	else \
		echo "Warning: $(PHP_INSTALL_DIR) is not writable."; \
		echo "Skipping system installation of php binding."; \
	fi
if WITH_SWIG_PHP_DATA
	-mkdir -p $(DESTDIR)$(PHP_INSTALL_DATADIR);
	if test -w $(DESTDIR)$(PHP_INSTALL_DATADIR); then \
		(cd $(DESTDIR)$(PHP_INSTALL_DATADIR); \
			cp -f $(DESTDIR)$(pkgphpdir)/gv.php gv.php;) \
	else \
		echo "Warning: $(PHP_INSTALL_DATADIR) is not writable."; \
		echo "Skipping system installation of php binding."; \
	fi
endif
endif
if WITH_PYTHON3
	-mkdir -p $(DESTDIR)$(PYTHON3_INSTALL_DIR);
	if test -w $(DESTDIR)$(PYTHON3_INSTALL_DIR); then \
		(cd $(DESTDIR)$(PYTHON3_INSTALL_DIR); \
			cp -f $(DESTDIR)$(pkgpython3dir)/libgv_python3.so _gv.so; \
			cp -f $(DESTDIR)$(pkgpython3dir)/gv.py gv.py;) \
	else \
		echo "Warning: $(PYTHON3_INSTALL_DIR) is not writable."; \
		echo "Skipping system installation of python3 binding."; \
	fi
endif
if WITH_RUBY
	-mkdir -p $(DESTDIR)$(RUBY_INSTALL_DIR);
	if test -w $(DESTDIR)$(RUBY_INSTALL_DIR); then \
		(cd $(DESTDIR)$(RUBY_INSTALL_DIR); \
			cp -f $(DESTDIR)$(pkgrubydir)/libgv_ruby.so gv.so;) \
	else \
		echo "Warning: $(RUBY_INSTALL_DIR) is not writable."; \
		echo "Skipping system installation of ruby binding."; \
	fi
endif
if WITH_TCL
	-mkdir -p $(DESTDIR)$(TCL_INSTALL_DIR);
	if test -w $(DESTDIR)$(TCL_INSTALL_DIR)/; then \
		(cd $(DESTDIR)$(TCL_INSTALL_DIR); \
			cp -rf $(DESTDIR)$(pkgtcldir) $(PACKAGE_NAME);) \
	else \
		echo "Warning: $(TCL_INSTALL_DIR) is not writable."; \
		echo "Skipping system installation of tcl bindings."; \
	fi
endif
endif

# removal of installs into $(xxx_INSTALL_DIR) fail if root
#     has installed a system copy
uninstall-hook:
	-rm -rf $(DESTDIR)$(pkgsharpdir);
	-rm -rf $(DESTDIR)$(pkggodir);
	-rm -rf $(DESTDIR)$(pkgguiledir);
	-rm -rf $(DESTDIR)$(pkgiodir);
	-rm -rf $(DESTDIR)$(pkgjavadir);
	-rm -rf $(DESTDIR)$(pkgluadir);
	-rm -rf $(DESTDIR)$(LUA_INSTALL_DIR)/gv.so;
	-rm -rf $(DESTDIR)$(pkgperldir);
	-rm -rf $(DESTDIR)$(PERL_INSTALL_DIR)/gv.so $(DESTDIR)$(PERL_INSTALL_DIR)/gv.pm;
	-rm -rf $(DESTDIR)$(pkgphpdir);
	-rm -rf $(DESTDIR)$(PHP_INSTALL_DIR)/gv.so $(DESTDIR)$(PHP_INSTALL_DATADIR)/gv.php;
	-rm -rf $(DESTDIR)$(pkgpython3dir);
	-rm -rf $(DESTDIR)$(PYTHON3_INSTALL_DIR)/_gv.so $(DESTDIR)$(PYTHON3_INSTALL_DIR)/gv.py;
	-rm -rf $(DESTDIR)$(pkgRdir);
	-rm -rf $(DESTDIR)$(pkgrubydir);
	-rm -rf $(DESTDIR)$(RUBY_INSTALL_DIR)/gv.so;
	-rm -rf $(DESTDIR)$(pkgtcldir);
	-rm -rf $(DESTDIR)$(TCL_INSTALL_DIR)/$(PACKAGE_NAME)-$(PACKAGE_VERSION);

EXTRA_DIST = mkpkgindex.py tcl-compat.h

DISTCLEANFILES = pkgIndex.tcl
