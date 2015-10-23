AC_PREREQ([2.69])
m4_define([ntfs_linker_version], [0.1.0])
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

AX_APPEND_COMPILE_FLAGS([-W -Wall -Wextra -Wnon-virtual-dtor -pedantic -pipe -O3 -g], [NL_CXXFLAGS])
AX_APPEND_LINK_FLAGS([-g -pthread], [NL_LDFLAGS])

AX_APPEND_COMPILE_FLAGS([-pthread -D__VERSION="'\"ntfs_linker_version\"'"], [NL_CPPFLAGS])

# pkg-config doesn't look in /usr/local/lib on some systems
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig

PKG_CHECK_MODULES([BFIO], [libbfio], [AC_DEFINE([HAVE_LIBBFIO], [1], [Lets libewf, libvshadow know we have libbfio.])])
PKG_CHECK_MODULES([EWF], [libewf])
PKG_CHECK_MODULES([SQLITE], [sqlite3])
PKG_CHECK_MODULES([VSHADOW], [libvshadow])
PKG_CHECK_MODULES([CERROR], [libcerror])

# zlib is a dependency of libtsk
PKG_CHECK_MODULES([ZLIB], [zlib])

AX_CHECK_LIBRARY([TSK], [tsk/libtsk.h], [tsk],
                 [TSK_LIBS=-ltsk],
                 [AC_MSG_ERROR([Failed to find libtsk])])
AC_SUBST([TSK_LIBS])

# Check that libvshadow has bfio
AC_EGREP_CPP(yes,
             [#include <libvshadow.h>
              #ifndef LIBVSHADOW_HAVE_BFIO
              yes
              #endif
              ], AC_MSG_ERROR([libvshadow does not have libbfio.]))

# Check that libtsk doesn't have multithread
AC_EGREP_CPP(yes,
             [#include <tsk/libtsk.h>
              #ifdef TSK_MULTITHREAD_LIB
              yes 
              #endif
              ], AC_MSG_ERROR([libtsk has multithreading but should not.]))

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

AX_BOOST_SYSTEM
if test "x$ax_cv_boost_system" != "xyes"; then
  AC_MSG_ERROR([Failed to find Boost system library.])
fi

AX_BOOST_FILESYSTEM
if test "x$ax_cv_boost_filesystem" != "xyes"; then
  AC_MSG_ERROR([Failed to find Boost filesystem library.])
fi


###############################################################################
# Scope test framework
AC_ARG_WITH([scope],
  [AS_HELP_STRING([--with-scope=ARG],
    [use Scope headers from the specified location])],
  [SCOPE_PATH="$withval"],
  [SCOPE_PATH="vendors/scope"])

if test "x$with_scope" != "xno"; then
  # test Scope without adding its path to CPPFLAGS generally
  SCOPE_CPPFLAGS="-isystem `realpath $SCOPE_PATH`"
  CPPFLAGS_saved="$CPPFLAGS"
  CPPFLAGS="$SCOPE_CPPFLAGS"
  export CPPFLAGS

  AC_CHECK_HEADERS([scope/test.h],[scope_ok="yes"])

  CPPFLAGS="$CPPFLAGS_saved"

  if test "x$scope_ok" = "xyes"; then
    AC_DEFINE(HAVE_SCOPE,1,[Define to 1 if Scope test framework is available.])
    AC_SUBST([SCOPE_CPPFLAGS])
  fi
fi

# Tell the user why not if he won't be able to compile the tests
if test "x$scope_ok" != "xyes"; then
  AC_MSG_WARN([])
  AC_MSG_WARN([You will be unable to compile and run the tests because:])
  AC_MSG_WARN([])
  if test "x$scope_ok" != "xyes"; then
    AC_MSG_WARN([  * Scope test framework is unavailable])
  fi
  AC_MSG_WARN([])
fi
###############################################################################


# Ensure that we statically link everything which is not a system lib;
# -all-static includes libstdc++ and libgcc, which are excluded by
# -static.
if test "x$enable_shared" != "xyes"; then
  CXXLD="$CXX -all-static"
else
  CXXLD="$CXX"
fi
AC_SUBST([CXXLD])

AC_DEFUN([SF_APPEND], [{
  if test -n "[$]$1"; then
    if test -n "$2"; then
      AS_VAR_APPEND($1, " $2")
    fi
  else
    $1=[$]$2
  fi
}])

# collect the flags from everything which might set some
for lib in EWF VSHADOW SQLITE BFIO TSK BOOST ZLIB CERROR; do
  # fold CFLAGS into CXXFLAGS since everything here is C++
  h="${lib}_CXXFLAGS"
  t=$(eval echo \"\$${lib}_CFLAGS\")
  SF_APPEND($h, $t)

  for var in CPPFLAGS CXXFLAGS LDFLAGS LIBS; do
    h="NL_$var"
    t=$(eval echo \"\$${lib}_$var\")
    SF_APPEND($h, $t)
  done
done

# yay, inconsistent variable naming
NL_LIBS="$NL_LIBS $BOOST_PROGRAM_OPTIONS_LIB $BOOST_FILESYSTEM_LIB $BOOST_SYSTEM_LIB"

AC_SUBST([NL_CPPFLAGS])
AC_SUBST([NL_CXXFLAGS])
AC_SUBST([NL_LDFLAGS])
AC_SUBST([NL_LIBS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
