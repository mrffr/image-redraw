CC=gcc

LIBS=-lImlib2

all: redraw.c 
	${CC} -o redraw redraw.c ${LIBS} -O2

debug: redraw.c
	${CC} -g -o debug redraw.c ${LIBS}
