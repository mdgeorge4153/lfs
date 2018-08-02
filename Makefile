default: all
all:     main

main: blockstore.o files.o types.o main.o
	gcc -g -o $@ $^

clean:
	git clean -fdX

%.o: %.c
	gcc -g -c -o $@ $^

