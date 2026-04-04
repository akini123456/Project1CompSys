CC = gcc
CFLAGS = -Wall -O2

all: project1

project1: project1.o
	$(CC) $(CFLAGS) -o project1 project1.o

project1.o: project1.c
	$(CC) $(CFLAGS) -c project1.c

clean:
	rm -f project1.o project1 input.txt output.txt trace.csv