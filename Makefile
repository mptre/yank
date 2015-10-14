VERSION = 0.4.0

PREFIX  ?= /usr/local
YANKCMD ?= xsel

CFLAGS   += -Os -pedantic -std=c99 -Wall -Werror -Wextra
CPPFLAGS += -DVERSION=\"${VERSION}\" -DYANKCMD=\"${YANKCMD}\"

yank: yank.c
	${CC} ${CFLAGS} $^ -o $@ ${LDFLAGS} ${CPPFLAGS}

clean:
	rm -f yank

install: yank
	@echo "yank -> ${PREFIX}/bin/yank"
	@mkdir -p "${PREFIX}/bin"
	@cp -f yank "${PREFIX}/bin"
	@chmod 755 "${PREFIX}/bin/yank"
	@echo "yank.1 -> ${PREFIX}/share/man/man1/yank.1"
	@mkdir -p "${PREFIX}/share/man/man1"
	@cp -f yank.1 "${PREFIX}/share/man/man1/yank.1"
	@chmod 644 "${PREFIX}/share/man/man1/yank.1"

.PHONY: clean install

-include config.mk
