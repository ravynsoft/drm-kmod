# $FreeBSD$

SRCDIR=	${.CURDIR:H}/drivers/gpu/drm/qxl

.PATH:	${SRCDIR} ${SRCDIR}/../

.include "../kconfig.mk"

KMOD= 	qxl
SRCS= 	\
	qxl_cmd.c \
	qxl_debugfs.c \
	qxl_display.c \
	qxl_draw.c \
	qxl_drv.c \
	qxl_dumb.c \
	qxl_gem.c \
	qxl_image.c \
	qxl_ioctl.c \
	qxl_irq.c \
	qxl_kms.c \
	qxl_object.c \
	qxl_prime.c \
	qxl_release.c \
	qxl_ttm.c

CLEANFILES+= ${KMOD}.ko.full ${KMOD}.ko.debug

CFLAGS+= -I${.CURDIR:H}/linuxkpi/gplv2/include
CFLAGS+= -I${.CURDIR:H}/linuxkpi/bsd/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include
CFLAGS+= -I${.CURDIR:H}/linuxkpi/dummy/include

CFLAGS+= -I${.CURDIR:H}/include
CFLAGS+= -I${.CURDIR:H}/include/drm
CFLAGS+= -I${.CURDIR:H}/include/uapi
CFLAGS+= -I${SRCDIR:H:H}

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=${KMOD}_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD} -DLINUXKPI_VERSION=50000
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

SRCS	+=			\
	opt_acpi.h		\
	opt_compat.h		\
	opt_drm.h		\
	opt_platform.h		\
	opt_splash.h		\
	opt_syscons.h		\
	opt_teken.h		\
	acpi_if.h		\
	bus_if.h		\
	fb_if.h			\
	device_if.h		\
	pci_if.h		\
	pci_iov_if.h		\
	vnode_if.h

CWARNFLAGS.qxl_cmd.c= -Wno-cast-qual -Wno-format
CWARNFLAGS.qxl_dumb.c= -Wno-cast-qual -Wno-format
CWARNFLAGS.qxl_display.c= -Wno-cast-qual
CWARNFLAGS.qxl_image.c= -Wno-cast-qual -Wno-pointer-arith
CWARNFLAGS.qxl_ioctl.c= -Wno-pointer-arith
CWARNFLAGS.qxl_object.c= -Wno-pointer-arith
CWARNFLAGS.qxl_release.c= -Wno-pointer-arith -Wno-format
CWARNFLAGS.qxl_ttm.c= -Wno-format

.include <bsd.kmod.mk>
