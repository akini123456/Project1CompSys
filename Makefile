CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
.DEFAULT_GOAL := all

.PHONY: all problem1 problem2 run-problem1 run-problem1-exp1 run-problem1-exp2 \
	run-problem2-part1 run-problem2-q2 run-problem2-q3 clean

all: project1 problem1 problem2

project1: project1.o
	$(CC) $(CFLAGS) -o project1 project1.o

project1.o: project1.c
	$(CC) $(CFLAGS) -c project1.c

problem1: problem1_signals

problem1_signals: problem1_signals.o
	$(CC) $(CFLAGS) -o problem1_signals problem1_signals.o

problem1_signals.o: problem1_signals.c
	$(CC) $(CFLAGS) -c problem1_signals.c

problem2: problem2_signals

problem2_signals: problem2_signals.o
	$(CC) $(CFLAGS) -o problem2_signals problem2_signals.o

problem2_signals.o: problem2_signals.c
	$(CC) $(CFLAGS) -c problem2_signals.c

run-problem1: problem1_signals
	mkdir -p outputs
	./problem1_signals exp1 fast > outputs/problem1_default.txt 2>&1

run-problem1-exp1: problem1_signals
	mkdir -p outputs
	./problem1_signals exp1 fast > outputs/problem1_exp1.txt 2>&1

run-problem1-exp2: problem1_signals
	mkdir -p outputs
	./problem1_signals exp2 fast > outputs/problem1_exp2.txt 2>&1

run-problem2-part1: problem2_signals
	mkdir -p outputs
	./problem2_signals part1 fast > outputs/problem2_part1.txt 2>&1

run-problem2-q2: problem2_signals
	mkdir -p outputs
	./problem2_signals q2 fast > outputs/problem2_q2.txt 2>&1

run-problem2-q3: problem2_signals
	mkdir -p outputs
	./problem2_signals q3 fast > outputs/problem2_q3.txt 2>&1

clean:
	rm -f *.o project1 problem1_signals problem2_signals
	rm -f output.txt trace.csv outputs/*.txt outputs/*.csv
