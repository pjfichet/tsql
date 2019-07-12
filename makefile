# Packaging directory
DESTDIR=
# Prefix directory
PREFIX=$(HOME)/.local
# Where to place binaries
BINDIR=$(PREFIX)/bin
# Where to place libraries
MANDIR=$(PREFIX)/share/man

# C compiler
CC=cc
# compilier flags
CFLAGS=-Wall -O
# Linker flags
LDFLAGS=-lm -lsqlite3
OBJS=tsql.o

all: tsql

clean:
	rm -rf $(OBJS) tsql tsql.1

%.o: %.c
	$(CC) -c $(CFLAGS) $<
tsql: tsql.o
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.1 %.7: %.man
	sed -e "s|@BINDIR@|$(BINDIR)|g" $< > $@

$(DESTDIR)$(BINDIR)/%: %
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	install -c $< $@

$(DESTDIR)$(MANDIR)/man1/%: %
	test -d $(DESTDIR)$(MANDIR)/man1 || mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -c -m 644 $< $@

install: $(DESTDIR)$(BINDIR)/tsql $(DESTDIR)$(MANDIR)/man1/tsql.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/tsql
	rm -f $(DESTDIR)$(MANDIR)/man1/tsql.1

