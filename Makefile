# mrdwm - dynamic window manager
# See LICENSE file for copyright and license details.
include config.mk

SRC = drw.c dwm.c util.c
OBJ = ${SRC:.c=.o}

all: mrdwm

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	cp config.def.h $@

mrdwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f mrdwm dwm ${OBJ} dwm-${VERSION}.tar.gz

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f mrdwm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/mrdwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < dwm.1 > ${DESTDIR}${MANPREFIX}/man1/mrdwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/mrdwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/mrdwm \
		${DESTDIR}${MANPREFIX}/man1/mrdwm.1

.PHONY: all clean install uninstall
