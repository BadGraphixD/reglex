CC = gcc
CFLAGS = -Wall -O3

.PHONY: all test clean
all: lexer

lexer: lexer.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

lexer.o: lexer.c

lexer.c: lexer.reglex
	../reglex <lexer.reglex >lexer.c

clean:
	rm -f *.o lexer lexer.c

