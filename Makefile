# Makefile for romfser.

all: romfser

PACKAGE = romfser
CC = gcc
CFLAGS = -O2 -Wall -DVERSION=\"$(VERSION)\"#-g#
LDFLAGS =

prefix = /usr
bindir = $(prefix)/bin
mandir = $(prefix)/man

romfser: romfser.o
	$(CC) $(LDFLAGS) romfser.o -o romfser

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f romfser *.o

install: all install-bin

install-bin:
	mkdir -p $(PREFIX)$(bindir)
	install -m 755 romfser $(PREFIX)$(bindir)/


