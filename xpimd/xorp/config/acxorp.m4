AC_CHECK_FUNCS([random])

AC_CHECK_HEADERS([netinet/ip.h netinet/tcp.h])

case "${host_os}" in
    bsdi* )
	AC_DEFINE(HOST_OS_BSDI, 1, [Define to 1 if the OS is BSDI])
	AC_DEFINE(HOST_OS_NAME, "BSDI", [Define the OS name])
    ;;
    darwin* )
	AC_DEFINE(HOST_OS_MACOSX, 1, [Define to 1 if the OS is MacOS X])
	AC_DEFINE(HOST_OS_NAME, "MacOS X", [Define the OS name])
    ;;
    dragonfly* )
	AC_DEFINE(HOST_OS_DRAGONFLYBSD, 1, [Define to 1 if the OS is DragonFlyBSD])
	AC_DEFINE(HOST_OS_NAME, "DragonFlyBSD", [Define the OS name])
    ;;
    freebsd* )
	AC_DEFINE(HOST_OS_FREEBSD, 1, [Define to 1 if the OS is FreeBSD])
	AC_DEFINE(HOST_OS_NAME, "FreeBSD", [Define the OS name])
    ;;
    linux* )
	AC_DEFINE(HOST_OS_LINUX, 1, [Define to 1 if the OS is Linux])
	AC_DEFINE(HOST_OS_NAME, "Linux", [Define the OS name])
    ;;
    netbsd* )
	AC_DEFINE(HOST_OS_NETBSD, 1, [Define to 1 if the OS is NetBSD])
	AC_DEFINE(HOST_OS_NAME, "NetBSD", [Define the OS name])
    ;;
    openbsd* )
	AC_DEFINE(HOST_OS_OPENBSD, 1, [Define to 1 if the OS is OpenBSD])
	AC_DEFINE(HOST_OS_NAME, "OpenBSD", [Define the OS name])
    ;;
    solaris* )
	AC_DEFINE(HOST_OS_SOLARIS, 1, [Define to 1 if the OS is Solaris])
	AC_DEFINE(HOST_OS_NAME, "Solaris", [Define the OS name])
    ;;
    *cygwin )
	AC_MSG_ERROR([Cygwin is not (and will not be) supported.])
    ;;
    *mingw* )
	AC_DEFINE(HOST_OS_WINDOWS, 1, [Define to 1 if the OS is Windows])
	AC_DEFINE(HOST_OS_NAME, "Windows", [Define the OS name])
	_USING_WINDOWS="1"
	dnl This has to be here to stop the ssize_t test from telling lies.
	dnl Also, <routprot.h> and friends need the MPR version to be defined
	dnl before inclusion.
	CPPFLAGS="${CPPFLAGS} -D_NO_OLDNAMES -DMPR50=1"
    ;;
esac

