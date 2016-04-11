CFLAGS=-m32 -std=c11 -Wall -Werror
HEADERS=$(wildcard *.h)

test: crc8.o test.o
	gcc $(CFLAGS) -o test $^

%.o: %.c $(HEADERS)
	gcc $(CFLAGS) -c $<
