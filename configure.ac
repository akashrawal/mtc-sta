#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([MTC Standalone],[0.0.0],[],[mtc-sta],[https://github.com/akashrawal/mtc-sta])
AC_CONFIG_SRCDIR([mtc0-sta/common.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([-Wall -Werror silent-rules])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE
AM_PROG_AR
LT_INIT

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PATH_PROG([MDLC], [mdlc])

# Checks for libraries.
AC_CHECK_LIB([event_core], [event_base_new], [], [AC_MSG_ERROR(["could not find required library libevent_core"])])
PKG_CHECK_MODULES([MTC], [mtc0 >= 0.0.0])

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdint.h stdlib.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_CONFIG_FILES([Makefile
                 data/Makefile
                 data/mtc0-sta.pc
                 mtc0-sta/Makefile
                 ])
AC_OUTPUT
