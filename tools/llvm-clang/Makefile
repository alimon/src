#	$NetBSD: Makefile,v 1.3 2019/11/11 22:45:27 joerg Exp $

HOSTPROGNAME=	${MACHINE_GNU_PLATFORM}-clang
HOSTPROG_CXX=	${MACHINE_GNU_PLATFORM}-clang
HOST_SRCDIR=	external/apache2/llvm/bin/clang

.include "${.CURDIR}/../Makefile.host"

install.host: ${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-clang++
install.host: ${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-clang-cpp

${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-clang++:
	${_MKTARGET_INSTALL}
	mkdir -p ${HOST_BINDIR}
	${HOST_INSTALL_SYMLINK} ${HOSTPROGNAME} ${.TARGET}

${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-clang-cpp:
	${_MKTARGET_INSTALL}
	mkdir -p ${HOST_BINDIR}
	${HOST_INSTALL_SYMLINK} ${HOSTPROGNAME} ${.TARGET}
