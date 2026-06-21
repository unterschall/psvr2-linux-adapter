# SPDX-License-Identifier: GPL-2.0
CFLAGS   ?= -O2 -Wall -Wextra
CPPFLAGS += -I. -I../../kernel
PREFIX   ?= /usr/local
LIBDIR   ?= $(PREFIX)/lib
INCDIR   ?= $(PREFIX)/include
PCDIR    ?= $(LIBDIR)/pkgconfig

all: libpsvr2.a libpsvr2.so psvr2-monitor

libpsvr2.o: libpsvr2.c libpsvr2.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

libpsvr2.a: libpsvr2.o
	$(AR) rcs $@ $^

libpsvr2.so: libpsvr2.o
	$(CC) -shared -Wl,-soname,libpsvr2.so -o $@ $^

# The example only needs the public header, not the kernel uapi.
psvr2-monitor: psvr2-monitor.c libpsvr2.a
	$(CC) -I. $(CFLAGS) -o $@ psvr2-monitor.c libpsvr2.a

install: all
	install -Dm644 libpsvr2.h $(DESTDIR)$(INCDIR)/libpsvr2.h
	install -Dm644 libpsvr2.a $(DESTDIR)$(LIBDIR)/libpsvr2.a
	install -Dm755 libpsvr2.so $(DESTDIR)$(LIBDIR)/libpsvr2.so
	install -Dm644 libpsvr2.pc $(DESTDIR)$(PCDIR)/libpsvr2.pc

clean:
	$(RM) libpsvr2.o libpsvr2.a libpsvr2.so psvr2-monitor

.PHONY: all install clean
