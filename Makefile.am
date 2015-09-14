ACLOCAL_AMFLAGS = -I m4
 
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/utf8 $(SCOPE_CPPFLAGS) $(NL_CPPFLAGS) $(SQLITE_CPPFLAGS) $(BOOST_CPPFLAGS)
AM_CXXFLAGS = $(NL_CXXFLAGS) $(SQLITE_CXXFLAGS) $(BOOST_CXXFLAGS)
AM_LDFLAGS = $(NL_LDFLAGS) $(SQLITE_LDFLAGS) $(BOOST_LDFLAGS)

src_ntfs_linker_SOURCES = \
	src/aggregate.cpp \
	src/helper_functions.cpp \
	src/log.cpp \
	src/mft.cpp \
	src/ntfs_linker.cpp \
	src/progress.cpp \
	src/usn.cpp \
	src/sqlite_helper.cpp

src_ntfs_linker_LDADD = $(SQLITE_LIBS) \
	$(BOOST_FILESYSTEM_LIB) \
	$(BOOST_SYSTEM_LIB) \
	$(BOOST_PROGRAM_OPTIONS_LIB)

bin_PROGRAMS = src/ntfs_linker
 
check_PROGRAMS = test/test
TESTS = $(check_PROGRAMS)
 
test_test_SOURCES = \
	test/test.cpp \
	test/test_lnis.cpp \
	test/test_helper_functions.cpp \
	src/helper_functions.cpp
 
