CC=clang
CFLAGS=-g -std=c99 -Wall -D_GNU_SOURCE -O0

SRC=$(wildcard *.c)
OBJ=$(subst .c,.o,$(SRC))

proxy_core.a: $(OBJ)
	ar rcs proxy_core.a $(OBJ)

clean:
	rm -f $(OBJ)
	rm -f proxy_core.a

.PHONY: clean
