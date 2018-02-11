# $FreeBSD$

.PATH: ${SRCTOP}/sys/boot/common ${SRCTOP}/sys/boot/libsa

.PATH: ${LDRSRC} ${BOOTSRC}/libsa

CFLAGS+=-I${LDRSRC}

SRCS+=	boot.c commands.c console.c devopen.c interp.c 
SRCS+=	interp_backslash.c interp_parse.c ls.c misc.c 
SRCS+=	module.c

.if ${MACHINE} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SRCS+=	load_elf32.c load_elf32_obj.c reloc_elf32.c
SRCS+=	load_elf64.c load_elf64_obj.c reloc_elf64.c
.elif ${MACHINE} == "pc98"
SRCS+=	load_elf32.c load_elf32_obj.c reloc_elf32.c
.elif ${MACHINE_CPUARCH} == "aarch64"
SRCS+=	load_elf64.c reloc_elf64.c
.elif ${MACHINE_CPUARCH} == "arm"
SRCS+=	load_elf32.c reloc_elf32.c
.elif ${MACHINE_CPUARCH} == "powerpc"
SRCS+=	load_elf32.c reloc_elf32.c
SRCS+=	load_elf64.c reloc_elf64.c
.elif ${MACHINE_CPUARCH} == "sparc64"
SRCS+=	load_elf64.c reloc_elf64.c
.elif ${MACHINE_ARCH} == "mips64" || ${MACHINE_ARCH} == "mips64el"
SRCS+= load_elf64.c reloc_elf64.c
.elif ${MACHINE} == "mips"
SRCS+=	load_elf32.c reloc_elf32.c
.endif

.if defined(LOADER_NET_SUPPORT)
SRCS+=	dev_net.c
.endif

.if !defined(LOADER_NO_DISK_SUPPORT)
SRCS+=	disk.c part.c
CFLAGS+= -DLOADER_DISK_SUPPORT
.if !defined(LOADER_NO_GPT_SUPPORT)
CFLAGS+= -DLOADER_GPT_SUPPORT
.endif
.if !defined(LOADER_NO_MBR_SUPPORT)
CFLAGS+= -DLOADER_MBR_SUPPORT
.endif
.endif
.if !defined(LOADER_NO_GELI_SUPPORT)
CFLAGS+= -DLOADER_GELI_SUPPORT
.endif

.if defined(HAVE_BCACHE)
SRCS+=  bcache.c
.endif

.if defined(MD_IMAGE_SIZE)
CFLAGS+= -DMD_IMAGE_SIZE=${MD_IMAGE_SIZE}
SRCS+=	md.c
.else
CLEANFILES+=	md.o
.endif

# Machine-independant ISA PnP
.if defined(HAVE_ISABUS)
SRCS+=	isapnp.c
.endif
.if defined(HAVE_PNP)
SRCS+=	pnp.c
.endif

# Forth interpreter
.if defined(MK_FORTH)
SRCS+=	interp_forth.c
.include "${BOOTSRC}/ficl.mk"
.endif

.if defined(BOOT_PROMPT_123)
CFLAGS+=	-DBOOT_PROMPT_123
.endif

.if defined(LOADER_INSTALL_SUPPORT)
SRCS+=	install.c
.endif

.if defined(HAVE_ZFS)
CFLAGS+=	-DLOADER_ZFS_SUPPORT
CFLAGS+=	-I${ZFSSRC}
CFLAGS+=	-I${SYSDIR}/cddl/boot/zfs
.if ${MACHINE} == "amd64"
# Have to override to use 32-bit version of zfs library...
# kinda lame to select that there XXX
LIBZFSBOOT=	${BOOTOBJ}/zfs32/libzfsboot.a
.else
LIBZFSBOOT=	${BOOTOBJ}/zfs/libzfsboot.a
.endif
.endif

CLEANFILES+=	vers.c
VERSION_FILE?=	${.CURDIR}/version
.if ${MK_REPRODUCIBLE_BUILD} != no
REPRO_FLAG=	-r
.endif
vers.c: ${LDRSRC}/newvers.sh ${VERSION_FILE}
	sh ${LDRSRC}/newvers.sh ${REPRO_FLAG} ${VERSION_FILE} \
	    ${NEWVERSWHAT}

.if !empty(HELP_FILES)
CLEANFILES+=	loader.help
FILES+=		loader.help

loader.help: ${HELP_FILES}
	cat ${HELP_FILES} | awk -f ${LDRSRC}/merge_help.awk > ${.TARGET}
.endif
