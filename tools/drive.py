import os, pty, sys, time, select, fcntl, termios, struct, pyte, signal

COLS, ROWS = int(sys.argv[1]), int(sys.argv[2])
cmd = sys.argv[3].split()
keys = sys.argv[4:]

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.environ["LANG"] = "en_US.UTF-8"
    os.environ["TZ"] = "America/New_York"
    os.execvp(cmd[0], cmd)

fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)

def pump(t=0.25):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                data = os.read(fd, 65536)
            except OSError:
                return
            if not data: return
            stream.feed(data)

def dump(label):
    print("=== %s ===" % label)
    for line in screen.display:
        print(line.rstrip())
    print()

pump(0.5)
dump("start")
for k in keys:
    if k == "DUMP":
        dump("screen"); continue
    if k == "SLEEP":
        pump(0.6); continue
    seq = {"ENTER": b"\r", "ESC": b"\x1b", "TAB": b"\t", "BS": b"\x7f", "UP": b"\x1b[A", "DOWN": b"\x1b[B",
           "LEFT": b"\x1b[D", "RIGHT": b"\x1b[C", "SPACE": b" ", "CTRLU": b"\x15"}.get(k, None)
    if seq is None:
        seq = k.encode("utf-8")
    os.write(fd, seq)
    pump(0.15)
dump("final")
try:
    os.kill(pid, signal.SIGTERM)
except Exception: pass
