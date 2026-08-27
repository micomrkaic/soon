# soon store format

One UTF-8 text file, no BOM, LF line endings, `~/.local/share/soon/agenda.soon`
by default (`$SOON_FILE` or `-f` overrides). Human-editable; the app rewrites
the whole file on save. Git, Syncthing and rsync handle sync.

Lines are one of: a **record header** (starts in column 1), a
**field line** (indented by two spaces), a **comment** (`#` in column 1),
or blank. Fields belong to the most recent header. Unknown fields and
unknown header lines are kept verbatim across a rewrite; comments are
currently dropped (to be fixed).

## Places and the location timeline

    place Home       | 38.8048 -77.0469  | America/New_York
    place Ljubljana  | 46.0569  14.5058  | Europe/Ljubljana
    place Duluth     | 34.0029 -84.1446  | America/New_York

    home Home
    loc 2026-09-14 Ljubljana
    loc 2026-09-28 Home

`place NAME | LAT LON | ZONE` declares a place once. `home` is the default.
`loc DATE NAME` says "from this date onward I am at NAME"; every day inherits
the most recent `loc` at or before it, so a trip is two lines. Coordinates
are decimal degrees, north and east positive. The zone must be an IANA name;
the city table in the app fills all three fields on completion.

## Events

    E 2026-09-15 09:00 1h Coffee with Aljoša
      id: 7f3a9c1e
      at: Ljubljana
      tags: friends paper
      pri: 2
      note: Cankarjeva. Ask about the Veblen draft.

    E 2026-09-15 * Slovenia trip
      until: 2026-09-27

    E 2026-09-16 14:00 1h30 Dept. meeting
      repeat: weekly wed until 2026-12-31
      skip: 2026-10-14 2026-11-25

Header: `E DATE TIME DURATION TITLE` for timed events, `E DATE * TITLE` for
all-day. `TIME` is 24-hour `HH:MM`. `DURATION` is `45m`, `1h`, `1h30`, `2d`.
`TITLE` runs to the end of the line and may contain anything but a newline.

Timed events are in the wall clock of their `at:` place; if `at:` is absent,
in the wall clock of the day's location from the timeline. Either way the
zone is resolved at display time, so moving a trip moves its events with it.

Fields (all optional):

- `id:` eight hex characters, assigned on first save if missing; stable across
  edits so ICS export can carry a UID and imports can update in place
- `at:` place name (declared with `place`) or a one-off `NAME | LAT LON | ZONE`
- `until:` end date for multi-day all-day events
- `tags:` space-separated words
- `pri:` 1 (highest) to 5
- `repeat:` `daily`, `weekly [mon tue ...]`, `monthly [15 | 2nd wed | last fri]`,
  `yearly`, each with optional `every N`, and `until DATE` or `count N`
- `skip:` dates removed from a repeat (a count is not extended by skips)
- `note:` free text; repeat the field for more lines
- `who:` people, free text (Unicode names are the point)
- `url:` one per line

## Todos

    T 2026-09-20 Renew passport
      due: 2026-10-01
      pri: 1
      tags: admin

    T - Read Thain ch. 5
      done: 2026-08-25 22:10

    T 2026-09-01 Water the plants
      repeat: weekly mon thu
      done: 2026-08-27

Header: `T DATE TITLE` where `DATE` is the scheduled day, or `-` for none.
`due:` is the deadline; the month view shows a countdown from it. `done:`
carries the completion timestamp; a repeating todo's `done:` marks the last
completed occurrence, and the next one is generated from `repeat:`.
`status: cancelled` keeps a todo without completing it.

## Notes on the choices

- Header-then-fields, rather than key=value on one line, keeps titles
  unrestricted and keeps `grep '^E 2026-09'` useful.
- No escaping anywhere: a field is everything after `key: ` to end of line.
- ICS import and export are translations, not the native format; RRULEs that
  don't fit the `repeat:` grammar are imported as individual instances with a
  warning. Org export writes `SCHEDULED:`/`DEADLINE:` timestamps; org import
  reads those and headline tags.
- Config (slot size, working hours, palette, first weekday, default duration)
  lives in `~/.config/soon/config`, same `key: value` syntax, not in the store.

## Contacts (`contacts.soon`)

    C Simon Podnar
      id: 3f9a1c22
      phone: +386 41 123 456
      email: simon@example.si
      place: Ljubljana
      tags: friend neurology
      note: Dentist referral.

Same rules as the agenda file. Items refer to a contact by name in `who:`;
the name is matched case-insensitively, exact or unique prefix.
