CC ?= cc
CFLAGS ?= -O2
PREFIX ?= /usr/local

# Always applied, so `make CFLAGS="-O0 -g"` cannot drop the standard or the warnings.
DUBLI_CFLAGS = -std=c11 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -D_FILE_OFFSET_BITS=64 -Isrc

LIB_OBJ = src/util.o src/hash.o src/scan.o src/group.o src/report.o src/actions.o

all: dubli

dubli: $(LIB_OBJ) src/main.o
	$(CC) $(DUBLI_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

tests/test_unit: $(LIB_OBJ) tests/test_unit.o
	$(CC) $(DUBLI_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c src/dubli.h
	$(CC) $(DUBLI_CFLAGS) $(CFLAGS) -c -o $@ $<

test: dubli tests/test_unit
	./tests/test_unit
	bash tests/cli.sh ./dubli

install: dubli
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 dubli $(DESTDIR)$(PREFIX)/bin/dubli

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dubli

clean:
	rm -f dubli tests/test_unit src/*.o tests/*.o

.PHONY: all test install uninstall clean
