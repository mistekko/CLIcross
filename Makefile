PREFIX = ${HOME}/.local
CFLAGS = -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -g
LDFLAGS = ${LIBS}
CC = cc

SRC = picross.c
OBJ = ${SRC:.c=.o}

all: picross

.c.o:
	${CC} -c ${CFLAGS} $<

picross: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

install: all
	mkdir -p ${PREFIX}/bin
	chmod 755 picross
	cp -f picross ${PREFIX}/bin

clean:
	rm *.o picross
