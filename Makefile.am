ACLOCAL_AMFLAGS = -I m4
 
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/utf8 -I$(srcdir)/include/sqlite3 $(NL_CPPFLAGS)
AM_CXXFLAGS = $(NL_CXXFLAGS)
AM_LDFLAGS = $(NL_LDFLAGS)

src_ntfs_linker_SOURCES = \
	src/aggregate.cpp \
	src/helper_functions.cpp \
	src/log.cpp \
	src/mft.cpp \
	src/ntfs_linker.cpp \
	src/progress.cpp \
	src/sqlite3.c \
	src/usn.cpp

bin_PROGRAMS = src/ntfs_linker
 
check_PROGRAMS = test/test
TESTS = $(check_PROGRAMS)
 
test_test_SOURCES = \
	test/lnis.cpp
 
