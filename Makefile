VERSION=	1.1.0

YANKCMD=	xsel

PREFIX=		/usr/local
MANPREFIX=	${PREFIX}/share/man

PROG=	yank
OBJS=	yank.o

INSTALL_PROGRAM=	install -s -m 0755
INSTALL_MAN=		install -m 0644

CFLAGS+=	-pedantic -Wall -Werror -Wextra
CFLAGS+=	-DVERSION=\"${VERSION}\" -DYANKCMD=\"${YANKCMD}\"

DISTFILES=	CHANGELOG.md \
		LICENSE \
		Makefile \
		README.md \
		yank.1 \
		yank.c

all: ${PROG}

${PROG}: ${OBJS}
	${CC} -o ${PROG} ${OBJS} ${LDFLAGS}

clean:
	rm -f ${PROG} ${OBJS}

dist:
	set -e; \
	d=${PROG}-${VERSION}; \
	mkdir $$d; \
	for f in ${DISTFILES}; do \
		mkdir -p $$d/`dirname $$f`; \
		cp $$f $$d/$$f; \
	done; \
	tar czvf $$d.tar.gz $$d; \
	sha256 $$d.tar.gz >$$d.sha256; \
	rm -r $$d

distclean: clean
	rm -f ${PROG}-${VERSION}.tar.gz ${PROG}-${VERSION}.sha256

install: ${PROG}
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	${INSTALL_PROGRAM} ${PROG} ${DESTDIR}${PREFIX}/bin
	${INSTALL_MAN} yank.1 ${DESTDIR}${MANPREFIX}/man1

.PHONY: all clean dist distclean install

-include config.mk
