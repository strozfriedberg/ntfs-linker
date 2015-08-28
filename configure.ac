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
AX_APPEND_LINK_FLAGS([-g -static-libgcc -static-libstdc++ -pthread], [NL_LDFLAGS])

AX_APPEND_COMPILE_FLAGS([-pthread -D__VERSION="'\"ntfs_linker_version\"'"], [NL_CPPFLAGS])

PKG_CHECK_MODULES([SQLITE], [sqlite3])
 
AC_SUBST([NL_CPPFLAGS])
AC_SUBST([NL_CXXFLAGS])
AC_SUBST([NL_LDFLAGS])
 
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
