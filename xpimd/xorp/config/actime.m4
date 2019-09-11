dnl
dnl $XORP: xorp/config/actime.m4,v 1.1 2007/09/05 07:26:43 bms Exp $
dnl

dnl
dnl POSIX time checks.
dnl

AC_LANG_PUSH(C)

AC_CHECK_TYPES([struct timespec], [], [],
[
#include <time.h>
])

AC_LANG_POP(C)
AC_CACHE_SAVE
