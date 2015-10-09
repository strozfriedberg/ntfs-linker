ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/utf8 $(NL_CPPFLAGS)
AM_CXXFLAGS = $(NL_CXXFLAGS)
AM_LDFLAGS = $(NL_LDFLAGS)

src_common_SOURCES = \
	src/aggregate.cpp \
	src/helper_functions.cpp \
	src/log.cpp \
	src/mft.cpp \
	src/progress.cpp \
	src/usn.cpp \
	src/vss_handler.cpp

src_ntfs_linker_SOURCES = $(src_common_SOURCES)
src_ntfs_linker_SOURCES += \
	src/ntfs_linker.cpp \
	src/sqlite_helper.cpp

src_ntfs_linker_LDADD = $(NL_LIBS)

bin_PROGRAMS = src/ntfs_linker
 
check_PROGRAMS = test/test
TESTS = $(check_PROGRAMS)
 
test_test_SOURCES = $(src_common_SOURCES)
test_test_SOURCES += \
	test/test.cpp \
	test/test_helper_functions.cpp \
	test/test_lnis.cpp \
	test/test_usn.cpp

test_test_CPPFLAGS = $(AM_CPPFLAGS) $(SCOPE_CPPFLAGS)
test_test_LDADD = $(NL_LIBS)
