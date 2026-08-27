/* soon — keyboard-driven agenda and todo TUI
 * Copyright (C) 2026 Mico Mrkaić
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* utf8.h — UTF-8 codec and display width. No libc wcwidth: it is locale-bound
 * on glibc, stale on macOS, and absent in wasm. The width table here is
 * hand-maintained and covers combining marks, CJK, Hangul, fullwidth forms
 * and the common emoji blocks; to be replaced by a generated table.
 */
#ifndef SOON_UTF8_H
#define SOON_UTF8_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define UTF8_INVALID 0xFFFDu

/* Decode one code point; returns bytes consumed (1..4), or 0 at NUL.
 * Malformed input yields U+FFFD and consumes one byte. */
int utf8_decode(const char *s, uint32_t *cp);
/* Encode; returns bytes written (1..4). out needs 4 bytes. */
int utf8_encode(uint32_t cp, char *out);
/* Terminal cells occupied: 0, 1 or 2. Control characters count 0. */
int utf8_width(uint32_t cp);
/* Display width of a NUL-terminated string. */
int utf8_swidth(const char *s);
/* Number of code points. */
size_t utf8_len(const char *s);
/* Byte offset of the last code point start before the end (for backspace). */
size_t utf8_prev(const char *s, size_t byte_pos);
/* Bytes of s that fit in `width` cells without cutting a code point or a wide char. */
size_t utf8_fit(const char *s, int width, int *used_width);

/* Simple case folding: ASCII, Latin-1 and Latin Extended-A (č Š ž ü ł ...). Other scripts unchanged. */
uint32_t utf8_fold(uint32_t cp);
/* case-insensitive substring test on UTF-8 strings (needle empty -> true) */
bool utf8_icontains(const char *hay, const char *needle);

#endif
