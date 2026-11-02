#!/usr/bin/env python3
# soon — keyboard-driven agenda and todo TUI
# SPDX-License-Identifier: GPL-3.0-or-later
"""UI regression checks: drive the binary through tools/drive.py with the fixtures in
test/ui/, then look for expected text on the screens and for sanitizer complaints.
Run it on the sanitizer build:  make check-ui   (needs: pip install pyte)"""
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "test", "ui")
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "soon")
try:
    import pyte  # noqa: F401
except ImportError:
    print("uicheck: the pyte module is missing: pip install pyte  (--break-system-packages on Debian/Ubuntu)")
    sys.exit(2)

R = "\x1b[C"          # right arrow
CTRL_S = "\x13"
CTRL_ENTER = "\x1b[13;5u"
CTRL_U = "\x15"
FORBID = ["AddressSanitizer", "runtime error", "Segmentation", "LeakSanitizer"]

# name, editor script (or None), keys, expected substrings, forbidden substrings
CASES = [
    ("form: Enter advances, last field saves", None,
     ["T", "Blood work", "ENTER", ".", "ENTER", "ENTER", "2", "ENTER", "ENTER", "ENTER", "ENTER", "ENTER", "DUMP"],
     ["Blood work"], ["New todo"]),
    ("form: Ctrl-Enter saves from the title", None,
     ["T", "Via kitty", CTRL_ENTER, "DUMP"], ["Via kitty"], ["New todo"]),
    ("form: Ctrl-S saves", None,
     ["T", "Via ctrl s", CTRL_S, "DUMP"], ["Via ctrl s"], ["New todo"]),
    ("form: tags pane picks with the right arrow", None,
     ["T", "Eye exam", "ENTER", ".", "ENTER", "ENTER", "ENTER", "ENTER", "m", "e", "d", R, CTRL_S, "DUMP"],
     ["Eye exam  #medical"], []),
    ("tag filter narrows every view; A lists upcoming", None,
     ["#", "m", "e", "d", "ENTER", "A", "DUMP"], ["#medical  [upcoming]", "Dentist cleaning"], ["Book flights"]),
    ("tag filter clears", None,
     ["#", "m", "e", "d", "ENTER", "#", CTRL_U, "ENTER", "DUMP"], ["filter cleared"], []),
    ("o: quick add, then a bad line stays for fixing", None,
     ["o", "Call Simon #family", "ENTER", "junk every blurgh", "ENTER", "DUMP", "ESC", "DUMP"],
     ["Call Simon", "cannot parse; fix or Esc", "1 item added"], []),
    ("O: bulk capture, bad line reported", "fakeed-add.sh",
     ["O", "DUMP"], ["2 items added, 1 line dropped", "Pick up meds"], []),
    ("E: raw edit of the selected item", "fakeed-edit.sh",
     ["TAB", "E", "DUMP"], ["Daily stretch EDITED", "updated:"], []),
    ("u/U: undo and redo a delete", None,
     ["TAB", "d", "a", "DUMP", "u", "DUMP", "U", "DUMP"], ["undone", "redone"], []),
    ("search sheet", None,
     ["/", "medical", "ENTER", "DUMP", "ESC"], ["search: medical", "2 hits"], []),
    ("sheets cycle and the contacts shortcut", None,
     ["v", "v", "v", "DUMP", "C", "DUMP", "ESC"], ["sheet: todos", "sheet: contacts"], []),
    ("upcoming: Enter jumps, x toggles", None,
     ["A", "j", "x", "DUMP", "ENTER", "DUMP"], ["[upcoming]"], []),
    ("resize storm and help", None,
     ["?", "DUMP", "ESC", "+", "+", "+", "-", "-", "DUMP"], ["keys"], []),
]

def run(case):
    name, editor, keys, expect, forbid = case
    tmp = tempfile.mkdtemp(prefix="soon-ui-")
    agenda = os.path.join(tmp, "agenda.soon")
    shutil.copy(os.path.join(FIX, "agenda.soon"), agenda)
    env = dict(os.environ)
    env["VISUAL"] = os.path.join(FIX, editor) if editor else "/bin/false"
    env["EDITOR"] = env["VISUAL"]
    env["TMPDIR"] = tmp
    cmd = [sys.executable, os.path.join(ROOT, "tools", "drive.py"), "110", "34",
           "%s -f %s -C %s" % (BIN, agenda, os.path.join(FIX, "config"))] + keys
    out = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=60)
    text = out.stdout + out.stderr
    shutil.rmtree(tmp, ignore_errors=True)
    last = text.rsplit("=== screen ===", 1)[-1]          # case forbids apply to the last screen only
    problems = [f for f in FORBID if f in text] + [f for f in forbid if f in last]
    missing = [e for e in expect if e not in text]
    return problems, missing, text

def main():
    if not os.access(BIN, os.X_OK):
        print("uicheck: no binary at %s (make build/soon-asan)" % BIN); return 2
    failed = 0
    for case in CASES:
        problems, missing, text = run(case)
        ok = not problems and not missing
        print("%s  %s" % ("ok  " if ok else "FAIL", case[0]))
        if not ok:
            failed += 1
            for m in missing: print("      missing: %r" % m)
            for p in problems: print("      found:   %r" % p)
            if os.environ.get("UICHECK_VERBOSE"): print(text)
    print("uicheck      %3d passed, %d failed" % (len(CASES) - failed, failed))
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
