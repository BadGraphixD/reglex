CC = gcc
CFLAGS = -Wall --debug

.PHONY: all test clean
all: reglex

reglex: reglex.o regex2c/lib.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

regex2c.o: regex2c.c

regex2c/lib.o:
	cd regex2c && make lib

test: reglex
	cd test && make
	echo "test/lexer generated"

clean:
	rm -f *.o reglex
	cd test && make clean
	cd regex2c && make clean
