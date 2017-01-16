VERSION=	0.7.1

YANKCMD=	xsel

PREFIX=		/usr/local
MANPREFIX=	${PREFIX}/share/man

CFLAGS+=	-pedantic -Wall -Werror -Wextra
CPPFLAGS+=	-DVERSION=\"${VERSION}\" -DYANKCMD=\"${YANKCMD}\"

all: yank

yank: yank.c
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS} ${CPPFLAGS}

clean:
	rm yank

install: yank
	@mkdir -p ${PREFIX}/bin
	@mkdir -p ${MANPREFIX}/man1
	install -m 0755 yank ${PREFIX}/bin
	install -m 0644 yank.1 ${MANPREFIX}/man1

.PHONY: all clean install

-include config.mk
