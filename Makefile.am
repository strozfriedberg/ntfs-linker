ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(srcdir)/include $(NL_CPPFLAGS)
AM_CXXFLAGS = $(NL_CXXFLAGS)
AM_LDFLAGS = $(NL_LDFLAGS)

NL_LIB = src/libntfs_linker.la
NL_LIB_INT = src/libntfs_linkerint.la

lib_LTLIBRARIES = $(NL_LIB)

#if BUILD_DLL
#src_libntfs_linker_la_SOURCES = src/lib/version.rc
#else
src_libntfs_linker_la_SOURCES =
#endif
nodist_EXTRA_src_libntfs_linker_la_SOURCES = dummy.cpp # forces C++ linking

noinst_LTLIBRARIES = $(NL_LIB_INT)

src_libntfs_linkerint_la_SOURCES = \
	src/aggregate.cpp \
	src/controller.cpp \
	src/log.cpp \
	src/mft.cpp \
	src/progress.cpp \
	src/sqlite_util.cpp \
	src/usn.cpp \
	src/util.cpp \
	src/vss.cpp \
	src/walkers.cpp

src_libntfs_linker_la_LIBADD = $(NL_LIB_INT) $(NL_LIBS)
if BUILD_DLL
src_libntfs_linker_la_LDFLAGS = $(AM_LDFLAGS) $(NL_LIB_LDFLAGS) -avoid-version
else
src_libntfs_linker_la_LDFLAGS = $(AM_LDFLAGS) $(NL_LIB_LDFLAGS)
endif

src_ntfs_linker_SOURCES = src/ntfs_linker.cpp

src_ntfs_linker_LDADD = $(NL_LIB) $(NL_LIBS)

bin_PROGRAMS = src/ntfs_linker
 
check_PROGRAMS = test/test
TESTS = $(check_PROGRAMS)
 
test_test_SOURCES = \
	test/test.cpp \
	test/test_util.cpp \
	test/test_usn.cpp

test_test_CPPFLAGS = $(AM_CPPFLAGS) $(SCOPE_CPPFLAGS)
test_test_LDADD = $(NL_LIB_INT) $(NL_LIBS) 
