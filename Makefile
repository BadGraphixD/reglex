CC = gcc
CFLAGS = -Wall -O3
LT = lexer_template
LTF = $(LT)/$(LT).c

.PHONY: all test clean
all: reglex

reglex: reglex.o regex2c/lib.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

reglex.o: reglex.c $(LTF)

regex2c/lib.o:
	cd regex2c && make lib

test: reglex
	cd test && make
	echo "test/lexer generated"

clean:
	rm -f *.o reglex lexer_template/lexer_template.c
	cd test && make clean
	cd regex2c && make clean

$(LTF): $(LT)/start.c $(LT)/end.c $(LT)/main.c
	echo "static const unsigned char lexer_start[] = {" > $(LTF)
	xxd -i <$(LT)/start.c >> $(LTF)
	echo ", 0x0};" >> $(LTF)
	
	echo "static const unsigned char lexer_end[] = {" >> $(LTF)
	xxd -i <$(LT)/end.c >>   $(LTF)
	echo ", 0x0};" >> $(LTF)

	echo "static const unsigned char lexer_main[] = {" >> $(LTF)
	xxd -i <$(LT)/main.c >>  $(LTF)
	echo ", 0x0};" >> $(LTF)
