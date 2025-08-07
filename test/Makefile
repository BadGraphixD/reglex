CC = gcc
CFLAGS = -Wall -Werror

CDFLAGS = -pg -g
CRFLAGS = -O3

.PHONY: all debug release
all: lexer

debug: CFLAGS += $(CDFLAGS)
debug: lexer
release: CFLAGS += $(CRFLAGS)
release: lexer

lexer: lexer.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

lexer.o: lexer.c

lexer.c: lexer.reglex
	../reglex <lexer.reglex >lexer.c

clean:
	rm -f *.o lexer lexer.c

