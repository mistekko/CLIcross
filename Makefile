PREFIX = ${HOME}/.local
CFLAGS = -std=c99 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -g
LDFLAGS = ${LIBS}
CC = cc

SRC = clicross.c
OBJ = ${SRC:.c=.o}

all: clicross

.c.o:
	${CC} -c ${CFLAGS} $<

clicross: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

install: all
	mkdir -p ${PREFIX}/bin
	chmod 775 clicross
	cp -f clicross ${PREFIX}/bin

clean:
	rm *.o clicross
