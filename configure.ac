#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT(flashimg, 0.2, yargi@free.fr)
AC_CONFIG_SRCDIR([nand_ecc.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE
# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset strchr strdup])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
