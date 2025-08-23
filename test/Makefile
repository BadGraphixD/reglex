CC = gcc
CFLAGS = -Wall -Werror
LEX = ../reglex

CDFLAGS = -pg -g
CRFLAGS = -O3

.PHONY: all debug release
all: c_lexer html_js_lexer numbers_lexer

debug: CFLAGS += $(CDFLAGS)
debug: c_lexer html_js_lexer numbers_lexer
release: CFLAGS += $(CRFLAGS)
release: c_lexer html_js_lexer numbers_lexer

%_lexer: %_lexer.o
	$(CC) $(CFLAGS) $^ -o $@
	@echo " --- ./test/"$@ "generated"
	@echo ""
%_lexer.o: %_lexer.c
	$(CC) $(CFLAGS) -c -o $@ $<
%_lexer.c: %.reglex
	$(LEX) $^ -o $@

c_lexer: c_lexer.o
c_lexer.o: c_lexer.c
c_lexer.c: c.reglex

html_js_lexer: html_js_lexer.o
html_js_lexer.o: html_js_lexer.c
html_js_lexer.c: html_js.reglex

numbers_lexer: numbers_lexer.o
numbers_lexer.o: numbers_lexer.c
numbers_lexer.c: numbers.reglex

clean:
	rm -f *.o *_lexer *_lexer.c

