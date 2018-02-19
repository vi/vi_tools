PROGS=$(shell find . -maxdepth 1 -mindepth 1 \( -name '*.c' -o -name '*.cpp' \) -printf '%f\n' | cut -d. -f 1-1)

CFLAGS=-Wall -ggdb ${EXTRACFLAGS}
CXXFLAGS=-Wall -ggdb ${EXTRACXXFLAGS}

all: ${PROGS}

runso: runso.c
	${CC} -fPIC -pie -Wl,-E ${CFLAGS} $^ -ldl -o $@
