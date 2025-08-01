## Process this file with automake to produce Makefile.in

linkedman = neato.1 twopi.1 fdp.1 circo.1
if ENABLE_MAN_PDFS
linkedpdf = neato.1.pdf twopi.1.pdf fdp.1.pdf circo.1.pdf
else
linkedpdf =
endif
linkedprogram = neato$(EXEEXT) twopi$(EXEEXT) fdp$(EXEEXT) circo$(EXEEXT) \
	osage$(EXEEXT) patchwork$(EXEEXT)

if WITH_SFDP
linkedman += sfdp.1
if ENABLE_MAN_PDFS
linkedpdf += sfdp.1.pdf
endif
linkedprogram += sfdp$(EXEEXT)
endif

AM_CPPFLAGS = \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/lib/common \
	-I$(top_srcdir)/lib/gvc \
	-I$(top_srcdir)/lib/pathplan \
	-I$(top_srcdir)/lib/cgraph \
	-I$(top_srcdir)/lib/cdt

if ENABLE_SHARED
if ENABLE_LTDL
if ENABLE_STATIC
bin_PROGRAMS = dot dot_static
else
bin_PROGRAMS = dot dot_builtins
endif
dist_bin_SCRIPTS = dot_sandbox
else
if ENABLE_STATIC
bin_PROGRAMS = dot_static dot_builtins
else
bin_PROGRAMS = dot_builtins
endif
endif
else
if ENABLE_STATIC
bin_PROGRAMS = dot_static
endif
endif

dist_man_MANS = dot.1 dot_sandbox.1 osage.1 patchwork.1
if ENABLE_MAN_PDFS
pdf_DATA = dot.1.pdf osage.1.pdf patchwork.1.pdf
endif

dot_SOURCES = dot.c no_builtins.c
dot_CPPFLAGS = $(AM_CPPFLAGS) -DDEMAND_LOADING=1
dot_LDADD = \
	$(top_builddir)/lib/gvc/libgvc.la \
	$(top_builddir)/lib/cgraph/libcgraph.la

install-data-hook:
	(cd $(DESTDIR)$(man1dir); for i in $(linkedman); do rm -f $$i; $(LN_S) dot.1 $$i; done;)
	(cd $(DESTDIR)$(pdfdir); for i in $(linkedpdf); do rm -f $$i; $(LN_S) dot.1.pdf $$i; done;)

# create "neato", "fdp" etc as soflinks to "dot"
# run "dot -c", if possible, to create plugin config
install-exec-hook:
	(cd $(DESTDIR)$(bindir); if test -x dot$(EXEEXT); then for i in $(linkedprogram); do rm -f $$i; $(LN_S) dot$(EXEEXT) $$i; done; fi;)
	if test "x$(DESTDIR)" = "x" -a "x$(build)" = "x$(host)"; then if test -x $(bindir)/dot$(EXEEXT); then if test -x /sbin/ldconfig; then /sbin/ldconfig 2>/dev/null; fi; cd $(bindir); ./dot$(EXEEXT) -c; else cd $(bindir); ./dot_static$(EXEEXT) -c; fi; fi

uninstall-hook:
	(cd $(DESTDIR)$(man1dir); rm -f $(linkedman);)
	(cd $(DESTDIR)$(pdfdir); for i in $(linkedpdf); do rm -f $$i; done;)
	(cd $(DESTDIR)$(bindir); rm -f $(linkedprogram);)
	(cd $(DESTDIR)$(libdir); rm -f config;)

.1.1.pdf:
	rm -f $@; pdffile=$@; psfile=$${pdffile%pdf}ps; \
	$(GROFF) -Tps -man $< > $$psfile || { rm -f $$psfile; exit 1; }; \
	$(PS2PDF) $$psfile && rm -f $$psfile || { rm -f $$psfile; exit 1; }
SUFFIXES = .1 .1.pdf

dot_static_SOURCES = dot.c dot_builtins.cpp
dot_static_CPPFLAGS = $(AM_CPPFLAGS) -DDEMAND_LOADING=0
dot_static_LDADD = \
	$(top_builddir)/plugin/dot_layout/libgvplugin_dot_layout_C.la \
	$(top_builddir)/plugin/kitty/libgvplugin_kitty_C.la \
	$(top_builddir)/plugin/neato_layout/libgvplugin_neato_layout_C.la \
	$(top_builddir)/plugin/core/libgvplugin_core_C.la \
	$(top_builddir)/plugin/vt/libgvplugin_vt_C.la \
	$(top_builddir)/lib/gvc/libgvc_C.la \
	$(top_builddir)/lib/pathplan/libpathplan_C.la \
	$(top_builddir)/lib/xdot/libxdot_C.la \
	$(top_builddir)/lib/cgraph/libcgraph_C.la \
	$(top_builddir)/lib/cdt/libcdt_C.la \
	$(top_builddir)/lib/util/libutil_C.la \
	$(PANGOCAIRO_LIBS) $(PANGOFT2_LIBS) $(GTS_LIBS) $(EXPAT_LIBS) $(Z_LIBS) $(IPSEPCOLA_LIBS) $(MATH_LIBS)

dot_builtins_SOURCES = dot.c dot_builtins.cpp
dot_builtins_CPPFLAGS = $(AM_CPPFLAGS) -DDEMAND_LOADING=1
dot_builtins_LDADD = \
	$(top_builddir)/plugin/dot_layout/libgvplugin_dot_layout.la \
	$(top_builddir)/plugin/kitty/libgvplugin_kitty.la \
	$(top_builddir)/plugin/neato_layout/libgvplugin_neato_layout.la \
	$(top_builddir)/plugin/core/libgvplugin_core.la \
	$(top_builddir)/plugin/vt/libgvplugin_vt.la \
	$(top_builddir)/lib/gvc/libgvc.la \
	$(top_builddir)/lib/pathplan/libpathplan.la \
	$(top_builddir)/lib/xdot/libxdot.la \
	$(top_builddir)/lib/cgraph/libcgraph.la \
	$(top_builddir)/lib/cdt/libcdt.la \
	$(GTS_LIBS) $(EXPAT_LIBS) $(Z_LIBS) $(IPSEPCOLA_LIBS) $(MATH_LIBS)

if WITH_QUARTZ
dot_static_LDADD += $(top_builddir)/plugin/quartz/.libs/libgvplugin_quartz_C.a $(QUARTZ_LIBS)
dot_builtins_LDADD += $(top_builddir)/plugin/quartz/libgvplugin_quartz.la $(QUARTZ_LIBS)
endif

if WITH_LIBGD
dot_static_LDADD += $(top_builddir)/plugin/gd/.libs/libgvplugin_gd_C.a $(GDLIB_LIBS)
dot_builtins_LDADD += $(top_builddir)/plugin/gd/libgvplugin_gd.la $(GDLIB_LIBS)
endif

if WITH_PANGOCAIRO
dot_static_LDADD += $(top_builddir)/plugin/pango/.libs/libgvplugin_pango_C.a $(PANGOCAIRO_LIBS) $(PANGOFT2_LIBS)
dot_builtins_LDADD += $(top_builddir)/plugin/pango/libgvplugin_pango.la $(PANGOCAIRO_LIBS)
if WITH_WEBP
dot_static_LDADD += $(top_builddir)/plugin/webp/.libs/libgvplugin_webp_C.a $(WEBP_LIBS)
dot_builtins_LDADD += $(top_builddir)/plugin/webp/libgvplugin_webp.la $(WEBP_LIBS)
endif
endif

if WITH_LASI
dot_static_LDADD += $(top_builddir)/plugin/lasi/.libs/libgvplugin_lasi_C.a $(LASI_LIBS)
dot_builtins_LDADD += $(top_builddir)/plugin/lasi/libgvplugin_lasi.la $(LASI_LIBS)
endif

DISTCLEANFILES = $(pdf_DATA)
