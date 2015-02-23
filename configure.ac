#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT(ccfsyncd, 0.0.1, N/A)
AM_INIT_AUTOMAKE([1.12 foreign no-define])

AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

AC_DEFINE([NEED_INIT_THREADS], [0], [Determines whether we need to call glibs g_thread_init])
# Checks for library functions.
need_gthreadinit=0
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset strdup strerror strtol])
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.28.0])
AC_CHECK_LIB(gobject-2.0, g_thread_init, need_gthreadinit=1 )
PKG_CHECK_MODULES(GTHREAD, [gthread-2.0])
PKG_CHECK_MODULES(CURL, [libcurl >= 7.19.0])
PKG_CHECK_MODULES(JANSSON, [jansson >= 2.4])
PKG_CHECK_MODULES(OPENSSL, [openssl >= 0.9])

# RHEL 6 ships with an older version of glib, which needs to initialise threads 
# before using them. We need to detect this, as its deprecated in the version 
# shipping with Ubuntu
if test "$need_gthreadinit" = "1" ; then
  AC_MSG_NOTICE([ghthreadinit found])
  AC_DEFINE([NEED_INIT_THREADS], [1])
  PKG_CHECK_MODULES(GOBJECT, [gobject-2.0])
  LDFLAGS="$LDFLAGS -lgobject-2.0"
fi

if test "$need_gthreadinit" = "0" ; then
  AC_MSG_NOTICE([gthreadinit not needed...])
fi

LDFLAGS="$LDFLAGS -lpthread"
LDADD="$LDADD -lpthread"

AC_CONFIG_FILES([
 Makefile
  src/Makefile
  ])

AC_OUTPUT
