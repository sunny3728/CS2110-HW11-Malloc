# Makefile for CS 2110 Homework 11, Fall 2017

CC = gcc
CFLAGS = -std=c99 -pedantic -Wall -Werror -Wextra -g
CHECK_LIBS = $(shell pkg-config --cflags --libs check)

# The C and H files
CFILES = my_malloc.c malloc_suite.c tests.c
HFILES = my_malloc.h my_sbrk.h malloc_suite.h
OFILES = $(patsubst %.c,%.o,$(CFILES))

.PHONY: run-tests run-gdb run-valgrind clean

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $<

tests: $(OFILES)
	$(CC) $(CFLAGS) $^ -o $@ $(CHECK_LIBS)

run-tests: tests
	./tests

run-gdb: tests
	CK_FORK=no gdb ./tests

run-valgrind: tests
	CK_FORK=no valgrind --leak-check=full --error-exitcode=1 --show-leak-kinds=all --errors-for-leak-kinds=all ./tests

clean:
	rm -rf tests $(OFILES)
