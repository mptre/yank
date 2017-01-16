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
	@echo "yank -> ${PREFIX}/bin/yank"
	@mkdir -p "${PREFIX}/bin"
	@cp -f yank "${PREFIX}/bin"
	@chmod 755 "${PREFIX}/bin/yank"
	@echo "yank.1 -> ${MANPREFIX}/man1/yank.1"
	@mkdir -p "${MANPREFIX}/man1"
	@cp -f yank.1 "${MANPREFIX}/man1/yank.1"
	@chmod 644 "${MANPREFIX}/man1/yank.1"

.PHONY: all clean install

-include config.mk
