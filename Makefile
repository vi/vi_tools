PROGS=$(shell find . -maxdepth 1 -mindepth 1 -name '*.c' -printf '%f\n' | cut -d. -f 1-1)

CFLAGS=-Wall -ggdb ${EXTRACFLAGS}

all: ${PROGS}

runso: runso.c
	${CC} -fPIC -pie -Wl,-E ${CFLAGS} $^ -ldl -o $@
