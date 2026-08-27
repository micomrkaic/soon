# soon — keyboard-driven agenda and todo TUI

Everything in here will be done soon.

C17, no dependencies beyond libc and libm.
Linux and macOS terminals now; the core is written so the same code can be
compiled to wasm later (the store parses/serializes text, the screen is a
cell grid, time zones go through one shim).

## Build and run

    make            # builds build/soon and runs the tests
    make install    # copies it to ~/.local/bin/soon
    soon             # opens the 3-month view at today

First run: an agenda file is created at `~/.local/share/soon/agenda.soon`
(override with `-f FILE` or `$SOON_FILE`), and a home location is guessed
from the machine's time zone so the sun panel works immediately. Change it
with `H` inside the app, or edit the file — it is plain UTF-8 text, see
FORMAT.md.

    soon sep 14          # open at a date; any go-to expression works
    soon -s 30           # 30-minute slots
    soon -a              # ASCII glyphs only
    soon | head          # not a terminal: prints what is coming up instead
    soon -p -d 14        # same, forced, two weeks ahead

## Screen

    row 0    soon  Thu 27 Aug 2026  14:58:46 EDT  ·  20:58 CEST in Kranj          [3 months]  9 items
    row 1    Tue 15 Sep 2026  W38 · @ Kranj  tz Europe/Ljubljana (UTC+2) · ☼ 06:40-19:14 · civil 06:10-19:45
    body     calendar grid (year / 3 months / month) or the slot grid (week / day)
    ...      agenda panel for the cursor day (calendar views), sized from the terminal
    status   messages, prompts
    menu     two rows of key shortcuts, nano-style (M hides them, -m starts without)

Row 0 carries a live clock (home zone, with seconds) and, when the cursor
day is somewhere in another zone, the time there. Row 1 follows the cursor:
the day's location (marked `@` when it differs from home), its time zone
as `tz <IANA name> (UTC offset)`, sunrise-sunset, civil twilight and day
length, dropping segments from the right on narrow terminals. `s` opens the
full panel with nautical and astronomical twilight.

## Keys

    h j k l / arrows   move by day; by time slot in week and day views
    . t                today
    [ ]  { }  ( )      previous / next week, month, year (any view; M-{ M-} also work)
    + Enter            zoom in:  year > 3 months > month > week > day
    - Backspace        zoom out
    n p  PgDn PgUp     previous / next period of the current view
    g                  go to a date:  14.9.  sep 14  +3w  fri  next mon  w38  eom
    Tab                into the day's items: j/k or arrows walk them, the panel
                       shows every attribute of the item under the cursor,
                       Enter opens it for editing, h/l keep walking day by day,
                       Esc leaves
    a  T               new event / new todo (form, see below)
    e x m d            edit / done / move (calendar picker) / delete the selection
    s                  sun and twilight panel for the day's location
    L  H               location for a range of days / home location
    z                  cycle slot size 5 10 15 30 60
    v                  sheets (see below)
    /                  search items and contacts; Enter jumps to the hit
    w  q  Q            save / save and quit / quit without saving
    M                  show / hide the key menu
    ?                  help

### Forms: dates and times are picked, not typed

Every form has a pane next to the fields showing the control for the
focused field:

    Date fields     a month calendar; arrows/hjkl move by day and week,
                    n/p by month, N/P by year, t today, - clears an optional
                    date (due, until). An empty due date starts from the
                    scheduled date, an empty until from the event date.
    Time            a strip of slots; up/down by slot, left/right by hour,
                    n = now. Typing 1430 still works.
    Duration        presets 15m 30m 45m 1h 1h30 2h 3h 4h 6h 8h 1d; up/down
                    steps through them, left/right by hour, or type 1h30.
    Place           a list to pick from with up/down: your own places first
                    (everything you have ever used, in bold), then the
                    built-in city table (~100 cities incl. Ljubljana, Kranj,
                    Bled, Koper, Alexandria VA, Duluth GA, Washington DC).
                    Typing narrows the list. A place not in it is entered as
                    `Name | lat lon | Europe/Zone` and becomes one of yours.
    Priority        left/right, or 0-5.

Tab/Shift-Tab move between fields, Enter saves, Esc cancels. On terminals
narrower than 76 columns the pane moves below the fields; below that it is
dropped and the field values are still adjustable with the same keys.

`a` and `T` pre-fill the cursor day and the next slot after now (or the
time cursor in week/day views). `e` reopens the same form on the selection.

### Sheets

`v` opens the spreadsheet views and cycles through them (`V` backwards,
Esc returns to the calendar). The first three are the working sheets:

- **days** — columns are consecutive days, rows are time slots (`z` cycles
  5/10/15/30/60 min), with an all-day row on top. Every cell is a target:
  Enter on an empty cell creates an event at that day and time, Enter on an
  occupied cell edits it (a chooser appears when several overlap), `T`
  adds a todo to the day, `d` deletes, `x` toggles a todo done. `[`/`]`
  and PgUp/PgDn scroll by a week, `{`/`}` by a month, `.` today.
- **locations by date** — one row per calendar day, endless both ways:
  date, place, zone, offset, sunrise, sunset, day length, item count.
  Enter picks a place for that day from the list; `f` fills the row's
  place down to a date chosen on the calendar; `L` opens the range form;
  `d` sets the day back to home. A change of place is underlined.
- **todos** — one row per todo: done, title, scheduled, due, priority,
  who, tags. Enter edits the cell with the matching control (calendar,
  list...), `e` opens the row's form, `a` adds, `d` deletes, `x` toggles.

Behind them: **events**, **places** (name, coordinates, zone, uses;
renaming updates everything that refers to the place, a place in use
cannot be deleted) and **contacts**.

### Recurrence

The Repeat field in the event and todo forms offers none, daily, weekdays,
weekly, every 2 weeks, monthly, monthly on the same weekday, yearly, and
"custom...", which takes the full grammar: `daily|weekly [mon tue ...]|
monthly [15 | 2nd wed | last fri]|yearly`, with `every N`, and `until DATE`
or `count N`. "Repeat until" is the end of the series. Occurrences appear
everywhere a day is shown; a 31st-of-month rule skips short months, as
RFC 5545 does. Deleting a repeating item asks whether to remove only that
occurrence (a `skip:` line) or the whole series. A repeating todo has one
done state per occurrence: `x` marks the shown day, and earlier
occurrences count as done too; the details panel shows the rule and the
next date. ICS export writes RRULE/EXDATE and import reads FREQ, INTERVAL,
BYDAY, BYMONTHDAY, UNTIL and COUNT (anything fancier is kept as text with
a warning); org uses `+1w`-style repeaters where they can express the rule
and a `:REPEAT:` property always.

### Configuration

`~/.config/soon/config` (or `-C FILE`, `$SOON_CONFIG`); `soon config`
prints a commented file with the defaults to start from:

    slot: 15               week and day views: 5 10 15 30 60
    sheet_slot: 30         the days sheet
    work_start: 08:00      views open here; hours outside are dimmed
    work_end: 18:00
    first_weekday: monday  monday | sunday | saturday (week numbers stay ISO)
    menu: on
    unicode: auto          auto | on | off
    palette: default       default | mono (weight and inverse only)
    color event: bright-white on blue bold
    color todo: 11         roles: cursor today busy busier event eventsel allday
                           todo tododone sel box panel err now field focus rule
                           titlebar; colours by name, bright-name, or 0-15

Lines that fail to parse are reported at startup and ignored.

### Search

`/` asks for text and lists every event, todo and contact containing it,
case-insensitively including č/š/ž and other Latin accents, items by date
first. Enter jumps to the item on its day with it selected (or to the
contact in the contacts sheet), `e` opens it for editing, `x` marks a todo
done, `/` searches again, Esc returns.

### Contacts

A second file, `contacts.soon` next to the agenda (or `-c FILE`,
`$SOON_CONTACTS`), holds name, phone, email, place, tags and a note per
person. Events and todos have a Who field that picks from it, so "call
Simon" carries his number: the details panel shows phone and email of the
linked contact. The contacts sheet edits them; `soon import vcf`
reads a vCard file from a phone or mail client and `soon export vcf`
writes one.

### Import and export

    soon export org [FILE]     org-mode: TODO/DONE headlines with SCHEDULED,
                               DEADLINE and CLOSED, events as active timestamps,
                               :ID: :LOCATION: :WHO: properties, notes as body
    soon export ics [FILE]     iCalendar: VEVENT (UTC times, DATE for all-day)
                               and VTODO, UID, categories, description
    soon export vcf [FILE]     vCard 3.0
    soon import org|ics|vcf FILE

FILE defaults to stdout for export; `-` is stdin for import. Imports merge:
an item with a known id (org `:ID:`, ics `UID`) is updated, otherwise an
item with the same kind, title and day is updated, otherwise it is added;
nothing is ever deleted. Foreign UIDs are kept so a re-import updates
rather than duplicates. Recurrence rules are kept on the item (`rrule:`)
but not expanded yet, with a warning.

### Locations

Every day has a location: home by default, or whatever `L` assigned. `L`
opens a form with Place, From (the cursor day) and Until (also the cursor
day, so a single day trip is one Enter). Set Until with the calendar for a
trip; clear it with `-` for a permanent move. When the range ends, the
location that was in effect after it is restored automatically. `H` sets
home. On first run home is guessed from the machine's time zone.

## Layout

    core/       pure logic, no I/O, no globals
      civil.*     calendar arithmetic (day numbers, ISO weeks, month math)
      datejump.*  the go-to-date expression parser
      sun.*       sunrise/sunset/twilight (NOAA equations)
      utf8.*      UTF-8 codec and display width
      model.*     items, places, location timeline; store text <-> memory
      cities.*    built-in gazetteer for place completion
      contacts.*  address book store and vCard codec
      org.c ics.c org-mode and iCalendar codecs (codecs.h)
      text.*      string utilities shared by the codecs
      config.*    the config file
      tz.h        time zone interface, implemented per platform
    ui/
      grid.*      cell grid with line-diffed VT rendering
      app.*       views, navigation, editing, overlays
    platform/
      tz_posix.c  tz.h via system zoneinfo
      term_posix.c raw mode, window size, key decoding
    tools/
      suncheck.c  CLI: suncheck DATE LAT LON ZONE
      drive.py    drives the app in a pty and prints screens (needs pyte)
    test/         one binary per core module

## Development

    make test               all unit tests (calendar arithmetic, date parser, sun,
                            time zones, UTF-8, store, codecs, config)
    tools/drive.py          drives the app in a pseudo-terminal and prints the
                            screens (needs `pip install pyte`), used for UI checks
    make dist               source tarball

The code has been fuzzed with random keys and resizes under AddressSanitizer
and UndefinedBehaviorSanitizer; a build for that is
`cc -std=c17 -g -O1 -fsanitize=address,undefined main.c core/*.c ui/*.c platform/*.c -lm`.

## License

GNU General Public License v3.0 or later — see [LICENSE](LICENSE).
Copyright (C) 2026 Mico Mrkaić.

## Status

Working: all five views, agenda panel, forms with calendar picker, navigation,
add/edit/delete of events and todos, location timeline, sun panel, autosave,
resize, Unicode names.

Not yet: comment preservation in the store file, the wasm build, editing a
single occurrence of a series differently from the rest.
