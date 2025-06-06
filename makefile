CC = gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-icccm -lX11

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SHAREDIR = $(PREFIX)/share

TARGET = wmp
SOURCE = wmp.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -f $(TARGET) $(DESTDIR)$(BINDIR)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)
	mkdir -p $(DESTDIR)$(SHAREDIR)/xsessions
	cp -f wmp.desktop $(DESTDIR)$(SHAREDIR)/xsessions
	mkdir -p $(HOME)/.config/wmp
	cp -n wmp.ini $(HOME)/.config/wmp/ 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(SHAREDIR)/xsessions/wmp.desktop

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean