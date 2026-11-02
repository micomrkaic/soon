VERSION  = 0.1.0
CC      ?= cc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
CFLAGS  += -DSOON_VERSION=\"$(VERSION)\" -DSOON_PREFIX=\"$(PREFIX)\"

# install layout: PREFIX=/usr/local for a system install; DESTDIR for packaging
PREFIX  ?= $(HOME)/.local
BINDIR   = $(DESTDIR)$(PREFIX)/bin
MANDIR   = $(DESTDIR)$(PREFIX)/share/man/man1
DOCDIR   = $(DESTDIR)$(PREFIX)/share/doc/soon
LDLIBS   = -lm

CORE     = core/civil.c core/datejump.c core/sun.c core/utf8.c core/model.c core/cities.c core/text.c core/contacts.c core/org.c core/ics.c core/config.c core/quick.c
UI       = ui/grid.c ui/app.c
PLATFORM = platform/tz_posix.c platform/term_posix.c
HDRS     = $(wildcard core/*.h ui/*.h)

TESTS    = build/test_civil build/test_datejump build/test_sun build/test_tz build/test_utf8 build/test_model build/test_codecs build/test_config build/test_quick

.PHONY: all test check-ui check clean install uninstall man dist

all: build/soon

build:
	mkdir -p build

build/soon: main.c $(CORE) $(UI) $(PLATFORM) $(HDRS) | build
	$(CC) $(CFLAGS) -o $@ main.c $(CORE) $(UI) $(PLATFORM) $(LDLIBS)

build/test_civil: test/test_civil.c core/civil.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_civil.c core/civil.c $(LDLIBS)

build/test_datejump: test/test_datejump.c core/datejump.c core/civil.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_datejump.c core/datejump.c core/civil.c $(LDLIBS)

build/test_sun: test/test_sun.c core/sun.c core/civil.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_sun.c core/sun.c core/civil.c $(LDLIBS)

build/test_tz: test/test_tz.c platform/tz_posix.c core/civil.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_tz.c platform/tz_posix.c core/civil.c $(LDLIBS)

build/test_utf8: test/test_utf8.c core/utf8.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_utf8.c core/utf8.c $(LDLIBS)

build/test_model: test/test_model.c core/model.c core/civil.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_model.c core/model.c core/civil.c $(LDLIBS)

build/test_codecs: test/test_codecs.c core/org.c core/ics.c core/contacts.c core/text.c core/model.c core/civil.c platform/tz_posix.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_codecs.c core/org.c core/ics.c core/contacts.c core/text.c core/model.c core/civil.c platform/tz_posix.c $(LDLIBS)

build/test_config: test/test_config.c core/config.c core/text.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_config.c core/config.c core/text.c $(LDLIBS)

build/test_quick: test/test_quick.c core/quick.c core/datejump.c core/model.c core/civil.c test/check.h | build
	$(CC) $(CFLAGS) -o $@ test/test_quick.c core/quick.c core/datejump.c core/model.c core/civil.c $(LDLIBS)

build/suncheck: tools/suncheck.c $(CORE) platform/tz_posix.c | build
	$(CC) $(CFLAGS) -o $@ tools/suncheck.c $(CORE) platform/tz_posix.c $(LDLIBS)

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

# sanitizer build for the UI checks (and for fuzzing by hand)
ASAN_CFLAGS = -std=c17 -O1 -g -fsanitize=address,undefined -Wall -DSOON_VERSION=\"$(VERSION)\" -DSOON_PREFIX=\"$(PREFIX)\"
build/soon-asan: main.c $(CORE) $(UI) $(PLATFORM) $(HDRS) | build
	$(CC) $(ASAN_CFLAGS) -o $@ main.c $(CORE) $(UI) $(PLATFORM) $(LDLIBS)

# drives the sanitizer build through the fixtures in test/ui (needs python3 + pyte)
check-ui: build/soon-asan
	python3 tools/uicheck.py build/soon-asan

check: test check-ui

install: build/soon
	install -d $(BINDIR) $(MANDIR) $(DOCDIR)
	install -m 755 build/soon $(BINDIR)/soon
	install -m 644 doc/soon.1 $(MANDIR)/soon.1
	install -m 644 README.md FORMAT.md LICENSE $(DOCDIR)
	@echo "installed: $(BINDIR)/soon  $(MANDIR)/soon.1  $(DOCDIR)/"
	@case ":$$PATH:" in *":$(PREFIX)/bin:"*) ;; *) echo "note: $(PREFIX)/bin is not on your PATH";; esac

uninstall:
	rm -f $(BINDIR)/soon $(MANDIR)/soon.1
	rm -rf $(DOCDIR)
	@echo "removed soon from $(PREFIX)"

man: doc/soon.1
	man doc/soon.1

clean:
	rm -rf build

dist: clean
	tar czf soon-$(VERSION).tar.gz --transform 's,^,soon-$(VERSION)/,' --exclude=build --exclude='*.tar.gz' --exclude=.git *
