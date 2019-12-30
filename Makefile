PROGS=$(shell find . -maxdepth 1 -mindepth 1 \( -name '*.c' -o -name '*.cpp' \) -printf '%f\n' | cut -d. -f 1-1)

CFLAGS=-Wall -ggdb ${EXTRACFLAGS}
CXXFLAGS=-Wall -ggdb ${EXTRACXXFLAGS}

all: ${PROGS}

runso: runso.c
	${CC} -fPIC -pie -Wl,-E ${CFLAGS} $^ -ldl -o $@

dump_tc_stats: dump_tc_stats.c
	${CC} ${CFLAGS} `pkg-config --cflags --libs libnl-3.0 libnl-route-3.0 libnl-cli-3.0` $^ -o $@

fstest: fstest.c
	${CC} ${CFLAGS} -DSIGNAL fstest.c -O2 -o fstest
