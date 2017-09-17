# Makefile for ush
#
# Vincent W. Freeh
# 

CC=gcc
CFLAGS=-g
SRC=main.c parse.c parse.h
OBJ=main.o parse.o

ush:	$(OBJ)
	$(CC) -o $@ $(OBJ)

