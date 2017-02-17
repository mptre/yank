VERSION=	0.8.0

YANKCMD=	xsel

PREFIX=		/usr/local
MANPREFIX=	${PREFIX}/share/man

PROG=		yank

INSTALL_PROGRAM=	install -s -m 0755
INSTALL_MAN=		install -m 0644

CFLAGS+=	-pedantic -Wall -Werror -Wextra
CPPFLAGS+=	-DVERSION=\"${VERSION}\" -DYANKCMD=\"${YANKCMD}\"

all: ${PROG}

${PROG}: yank.c
	${CC} ${CFLAGS} -o ${PROG} yank.c ${LDFLAGS} ${CPPFLAGS}

clean:
	rm ${PROG}

install: ${PROG}
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	${INSTALL_PROGRAM} ${PROG} ${DESTDIR}${PREFIX}/bin
	${INSTALL_MAN} yank.1 ${DESTDIR}${MANPREFIX}/man1

.PHONY: all clean install

-include config.mk
