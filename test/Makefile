CC = gcc
CFLAGS = -Wall --debug

.PHONY: all test clean
all: # reglex

# reglex: reglex.o
# 	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# regex2c.o: regex2c.c

clean:
	rm -f *.o # reglex

