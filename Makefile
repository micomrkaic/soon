VERSION  = 0.1.0
CC      ?= cc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
CFLAGS  += -DSOON_VERSION=\"$(VERSION)\"
LDLIBS   = -lm

CORE     = core/civil.c core/datejump.c core/sun.c core/utf8.c core/model.c core/cities.c core/text.c core/contacts.c core/org.c core/ics.c core/config.c core/quick.c
UI       = ui/grid.c ui/app.c
PLATFORM = platform/tz_posix.c platform/term_posix.c
HDRS     = $(wildcard core/*.h ui/*.h)

TESTS    = build/test_civil build/test_datejump build/test_sun build/test_tz build/test_utf8 build/test_model build/test_codecs build/test_config build/test_quick

.PHONY: all test clean install dist

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

install: build/soon
	install -d $(HOME)/.local/bin
	install -m 755 build/soon $(HOME)/.local/bin/soon

clean:
	rm -rf build

dist: clean
	tar czf soon-$(VERSION).tar.gz --transform 's,^,soon-$(VERSION)/,' --exclude=build --exclude='*.tar.gz' --exclude=.git *
