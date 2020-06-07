# Port Metadata
PORTNAME =		libwav
PORTVERSION =		0.1

MAINTAINER =		Andress Barajas
LICENSE =		Public Domain
SHORT_DESC =		Library for decoding WAV file headers (with KOS additions)

# No dependencies beyond the base system.
DEPENDENCIES =

# What files we need to download, and where from.
GIT_REPOSITORY =	git://github.com/andressbarajas/libwav.git

TARGET =		libwav.a
INSTALLED_HDRS =	sndwav.h
HDR_INSTDIR =		wav

# KOS Distributed extras (to be copied into the build tree)
KOS_DISTFILES =		KOSMakefile.mk libwav.c sndwav.c libwav.h sndwav.h

include ${KOS_PORTS}/scripts/kos-ports.mk
