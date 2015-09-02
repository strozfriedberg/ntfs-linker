AC_PREREQ([2.69])
m4_define([ntfs_linker_version], [0.0.2])
AC_INIT([ntfs-linker], [ntfs_linker_version], [zweger@strozfriedberg.com])
 
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/ntfs_linker.cpp])
AC_CONFIG_HEADERS([config.h])
 
AM_INIT_AUTOMAKE([1.12 subdir-objects foreign])
LT_INIT
 
AC_PROG_CXX
AM_PROG_CC_C_O
 
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([noext], [mandatory])

AX_APPEND_COMPILE_FLAGS([-W -Wall -Wextra -Wnon-virtual-dtor -pedantic -pipe -O3 -g -pg], [NL_CXXFLAGS])
AX_APPEND_LINK_FLAGS([-g -pthread], [NL_LDFLAGS])

AX_APPEND_COMPILE_FLAGS([-pthread -D__VERSION="'\"ntfs_linker_version\"'"], [NL_CPPFLAGS])

PKG_CHECK_MODULES([SQLITE], [sqlite3])

#
# Boost
#
if test "x$with_boost" = "xno"; then
  AC_MSG_ERROR([--without-boost specified, but Boost is mandatory.])
else
  case "$host" in
  *-*-mingw*)
    # AX_BOOST_BASE doesn't find the Boost libs for mingw, we help it out
    if test "$with_boost_libdir" = ""; then
      with_boost_libdir="/usr/${host}/sys-root/mingw/lib"
      AC_MSG_WARN([--with-boost-libdir not set. We are guessing ${with_boost_libdir}.])
    fi
    ;;
  esac

  AX_BOOST_BASE([1.49.0],
    [],
    [AC_MSG_ERROR([Failed to find usable Boost headers.])])
fi

AX_BOOST_PROGRAM_OPTIONS
if test "x$ax_cv_boost_program_options" != "xyes"; then
  AC_MSG_ERROR([Failed to find Boost program_options library.])
fi

# Ensure that we statically link everything which is not a system lib;
# -all-static includes libstdc++ and libgcc, which are excluded by
# -static.
if test "x$enable_shared" != "xyes"; then
  CXXLD="$CXX -all-static"
else
  CXXLD="$CXX"
fi
AC_SUBST([CXXLD])

AC_SUBST([NL_CPPFLAGS])
AC_SUBST([NL_CXXFLAGS])
AC_SUBST([NL_LDFLAGS])
 
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
