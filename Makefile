all: main.o
	gcc main.o -o timerd
	ln -sf timerd timerctl

main.o: main.c

clean:
	-rm -rf *.o timerd timerctl
