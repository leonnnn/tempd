CFLAGS=-m32 -Wall -Werror

test: crc8.o test.o
	gcc $(CFLAGS) -o test $^

%.o: %.c
	gcc $(CFLAGS) -c $<
