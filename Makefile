LIBELF_CFLAGS ?= $(shell pkg-config libelf --cflags)
LIBELF_LIBS ?= $(shell pkg-config libelf --libs)

REFTOOL_SRC = reftool.c
REFTOOL_OBJ = ${REFTOOL_SRC:.c=.o}

CFLAGS := -O2 -pipe -Wall -pedantic -std=gnu99 ${LIBELF_CFLAGS}

reftool: ${REFTOOL_OBJ}
	${CC} -o $@ ${REFTOOL_OBJ} ${LIBELF_LIBS}

.SUFFIXES: .o
