## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/lib/common \
	-I$(top_srcdir)/lib/pathplan \
	-I$(top_srcdir)/lib/gvc \
	-I$(top_srcdir)/lib/cgraph \
	-I$(top_srcdir)/lib/cdt \
	$(LIBGVC_CFLAGS)

noinst_LTLIBRARIES = libgvplugin_neato_layout_C.la

if WITH_WIN32
lib_LTLIBRARIES = libgvplugin_neato_layout.la
else
pkglib_LTLIBRARIES = libgvplugin_neato_layout.la
endif

libgvplugin_neato_layout_C_la_SOURCES = \
	gvplugin_neato_layout.c \
	gvlayout_neato_layout.c
libgvplugin_neato_layout_C_la_LIBADD = \
	$(top_builddir)/lib/patchwork/libpatchwork_C.la \
	$(top_builddir)/lib/circogen/libcircogen_C.la \
	$(top_builddir)/lib/osage/libosage_C.la \
	$(top_builddir)/lib/fdpgen/libfdpgen_C.la \
	$(top_builddir)/lib/twopigen/libtwopigen_C.la \
	$(top_builddir)/lib/neatogen/libneatogen_C.la \
	$(top_builddir)/lib/sparse/libsparse_C.la \
	$(top_builddir)/lib/rbtree/librbtree_C.la \
	$(top_builddir)/lib/util/libutil_C.la

libgvplugin_neato_layout_la_LDFLAGS = -version-info $(GVPLUGIN_VERSION_INFO)
libgvplugin_neato_layout_la_SOURCES = $(libgvplugin_neato_layout_C_la_SOURCES)
libgvplugin_neato_layout_la_LIBADD = $(libgvplugin_neato_layout_C_la_LIBADD) \
	$(top_builddir)/lib/gvc/libgvc.la \
	$(top_builddir)/lib/pathplan/libpathplan.la \
	$(top_builddir)/lib/cgraph/libcgraph.la \
	$(top_builddir)/lib/cdt/libcdt.la \
	$(GTS_LIBS) $(IPSEPCOLA_LIBS) $(MATH_LIBS)

# add a non-existent C++ source to force the C++ compiler to be used for
# linking, so the C++ standard library is included for our C++ dependencies
nodist_EXTRA_libgvplugin_neato_layout_la_SOURCES = fake.cxx

if WITH_SFDP
libgvplugin_neato_layout_C_la_LIBADD += $(top_builddir)/lib/sfdpgen/libsfdpgen_C.la
endif

if WITH_WIN32
libgvplugin_neato_layout_la_LDFLAGS += -no-undefined
endif
