#	$NetBSD: Makefile,v 1.7 2019/11/21 22:29:03 joerg Exp $

.include <bsd.own.mk>

INSTIMGBASE=	NetBSD-${DISTRIBVER}-amd64-uefi-install	# gives ${IMGBASE}.img

INSTIMAGEMB?=	1550			# for all installation binaries

PRIMARY_BOOT=		bootxx_ffsv1
SECONDARY_BOOT=		boot
SECONDARY_BOOT_ARG=	# unnecessary
EFIBOOT=		${WORKDIR}/usr/mdec/bootx64.efi
EFIBOOT+=		${WORKDIR}/usr/mdec/bootia32.efi
#EFIBOOT=		${.OBJDIR}/../../../sys/arch/i386/stand/efiboot/bootx64/bootx64.efi
#EFIBOOT+=		${.OBJDIR}/../../../sys/arch/i386/stand/efiboot/bootia32/bootia32.efi

USE_MBR=		yes
USE_GPT=		yes

CLEANFILES+=	boot.cfg

prepare_md_post:
	${TOOL_SED}							\
	    -e "s/@@MACHINE@@/${MACHINE}/"				\
	    -e "s/@@VERSION@@/${DISTRIBVER}/"				\
	    < ${.CURDIR}/boot.cfg.in > boot.cfg

DISTRIBDIR!= cd ${.CURDIR}/../.. ; pwd
SYSINSTDIR!= cd ${.CURDIR}/../../../usr.sbin/sysinst/arch/${MACHINE} && ${PRINTOBJDIR}

SPEC_EXTRA=		${.CURDIR}/spec.inst
IMGFILE_EXTRA=								\
	${.CURDIR}/etc.ttys		etc/ttys			\
	${.CURDIR}/etc.rc		etc/rc				\
	${.CURDIR}/install.sh		.				\
	${.OBJDIR}/boot.cfg		.				\
	${SYSINSTDIR}/sysinstmsgs.de	.				\
	${SYSINSTDIR}/sysinstmsgs.es	.				\
	${SYSINSTDIR}/sysinstmsgs.fr	.				\
	${SYSINSTDIR}/sysinstmsgs.pl	.				\
	${SYSINSTDIR}/sysinst		.

.include "${NETBSDSRCDIR}/distrib/common/bootimage//Makefile.installimage"
