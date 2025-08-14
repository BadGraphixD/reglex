CC = gcc
CFLAGS = -Wall -Werror
LIB_TARGET = lib

CDFLAGS = -pg -g
CRFLAGS = -O3

LT = lexer_template
LTF = $(LT)/$(LT).c

.PHONY: all debug release test clean
all: reglex

debug: CFLAGS += $(CDFLAGS)
debug: LIB_TARGET = lib_debug
debug: reglex
release: CFLAGS += $(CRFLAGS)
release: LIB_TARGET = lib_release
release: reglex

reglex: reglex.o regex2c/lib.o regex2c/not_enough_cli/bin/lib.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

reglex.o: reglex.c $(LTF)

regex2c/lib.o:
	@cd regex2c && make $(LIB_TARGET)

regex2c/not_enough_cli/bin/lib.o:
	@cd regex2c/not_enough_cli && make $(LIB_TARGET)

test: reglex
	@cd test && make
	@echo "test/lexer generated"

clean:
	rm -f *.o reglex lexer_template/lexer_template.c
	@cd test && make clean
	@cd regex2c && make clean

$(LTF): $(LT)/template.c $(LT)/main.c
	@echo "static const char lexer_template[] = {" > $(LTF)
	@xxd -i <$(LT)/template.c >> $(LTF)
	@echo ", 0x0};" >> $(LTF)

	@echo "static const char lexer_main[] = {" >> $(LTF)
	@xxd -i <$(LT)/main.c >>  $(LTF)
	@echo ", 0x0};" >> $(LTF)
