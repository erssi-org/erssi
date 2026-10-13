"""Harness for erssi live tests: the build installed under a temporary
directory, a fake IRC server (ircd.py), raw IRC clients and erssi instances
running in a pty, each with its own HOME and --home.

A test script calls main([test, ...]); it prints one line per test and exits
0 (all ok), 1 (a failure) or 77 (Skip raised: the reason is printed)."""
import fcntl
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import termios
import threading
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
ANSI = re.compile(r"\x1b(?:\[[0-9;?]*[ -/]*[@-~]|\][^\x07\x1b]*(?:\x07|\x1b\\)|[()][0-9A-Za-z]|[=>78])")


class Skip(Exception):
    """The test cannot run here (missing tool or feature)."""


def keep_work():
    return os.environ.get("KEEP_WORK") == "1"


def main(tests):
    ok = failed = 0
    try:
        for test in tests:
            name = getattr(test, "__name__", str(test))
            try:
                test()
                print(f"  ok   {name}", flush=True)
                ok += 1
            except Skip:
                raise
            except Exception as exc:  # AssertionError included
                if not isinstance(exc, AssertionError):
                    traceback.print_exc()
                print(f"  FAIL {name}: {exc or repr(exc)}", flush=True)
                failed += 1
    except Skip as exc:
        print(f"SKIP: {exc}", flush=True)
        if failed:  # a failure before the skip must not turn into a SKIP
            print(f"result: {ok} ok, {failed} failed", flush=True)
            sys.exit(1)
        sys.exit(77)
    print(f"result: {ok} ok, {failed} failed", flush=True)
    sys.exit(1 if failed else 0)


class Inst:
    """This build installed under <work>/inst (the installed layout)."""

    def __init__(self, root):
        found = list(root.rglob("bin/erssi"))
        mods = list(root.rglob("libirc_core.so"))
        if not found or not mods:
            raise Skip("installed tree incomplete (no bin/erssi or libirc_core.so)")
        self.root = root
        self.erssi = found[0]
        self.moddir = mods[0].parent
        irssi_pm = list(root.rglob("Irssi.pm"))
        self.perllib = irssi_pm[0].parent if irssi_pm else None
        self.has_perl = self.perllib is not None
        share = self.erssi.parent.parent / "share" / "irssi"
        self.scriptdir = share / "scripts"
        self.helpdir = share / "help"
        self.themedir = share / "themes"


def install(build, work):
    """meson install --destdir <work>/inst. The install step rebuilds what is
    out of date first: `meson test <name>` rebuilds only the test's own
    dependencies, so without that a named live test could run a stale binary."""
    build, work = Path(build), Path(work)
    if shutil.which("meson") is None:
        raise Skip("meson not found")
    dest = work / "inst"
    if not dest.exists():
        tmp = work / "inst.tmp"  # an interrupted install is never reused
        shutil.rmtree(tmp, ignore_errors=True)
        r = subprocess.run(["meson", "install", "-C", str(build), "--destdir", str(tmp),
                            "--quiet"], capture_output=True, text=True)
        if r.returncode != 0:
            raise AssertionError("meson install failed:\n" + r.stdout + r.stderr)
        tmp.rename(dest)
    return Inst(dest)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_port(port, timeout=5, proc=None):
    end = time.time() + timeout
    while time.time() < end:
        if proc is not None and proc.poll() is not None:
            return False
        try:
            socket.create_connection(("127.0.0.1", port), 1).close()
            return True
        except OSError:
            time.sleep(0.05)
    return False


class Ircd:
    """tests/live/ircd.py as a child process; the wire log is work/wire.log,
    every line in and out (with the time) is in work/ircd-trace.log.
    Usable as a context manager (stop() on exit)."""

    def __init__(self, work, extra_args=()):
        self.work = Path(work)
        self.extra = list(extra_args)
        self.proc = None
        self.wire = self.work / "wire.log"
        self.trace = self.work / "ircd-trace.log"

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()

    def start(self):
        """Start the server on a free port (retried: the port can be taken
        between choosing it and binding it). Returns the port."""
        for _ in range(4):
            port = free_port()
            self.proc = subprocess.Popen(
                [sys.executable, str(HERE / "ircd.py"), "--port", str(port), "--wire", str(self.wire),
                 "--trace", str(self.trace)] + self.extra)
            up = wait_port(port, 5, self.proc)
            time.sleep(0.2)  # a server that lost the bind race exits now
            if up and self.proc.poll() is None:
                return port
            self.stop()
        raise AssertionError("fake IRC server did not start")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()


class Client:
    """A bare IRC connection to the fake server, registered (001 seen)."""

    def __init__(self, port, nick, user="u"):
        self.nick = nick
        self.sock = socket.create_connection(("127.0.0.1", port), 5)
        self.buf = b""
        try:
            self.send(f"NICK {nick}")
            self.send(f"USER {user} 0 * :{nick}")
            self.expect(" 001 ")
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def send(self, line):
        self.sock.sendall(line.encode() + b"\r\n")

    def expect(self, substr, timeout=10):
        end = time.time() + timeout
        while True:
            while b"\r\n" in self.buf:
                line, self.buf = self.buf.split(b"\r\n", 1)
                text = line.decode(errors="replace")
                if text.startswith("PING"):
                    self.send("PONG" + text[4:])
                if substr in text:
                    return text
            left = end - time.time()
            if left <= 0:
                raise AssertionError(f"{self.nick}: did not see {substr!r}")
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                raise AssertionError(f"{self.nick}: connection closed waiting for {substr!r}")
            self.buf += data

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def config(port, nick, chatnet="E2E", channels=("#test",), extra="", servers=None):
    """An erssi config for the fake server. "@HOME@" and "@PERLLIB@" are
    replaced by Erssi.start() with the instance's --home and Perl library.
    servers: list of (chatnet, port); default [(chatnet, port)].
    cmd_queue_speed is 5 ms: with the default 2200 ms the post-connect
    commands (MODE, JOIN, MODE #chan, WHO) are paced one per 500 ms tick
    and a join takes ~5 s."""
    servers = servers or [(chatnet, port)]
    srv = ", ".join(f'{{ address = "127.0.0.1"; chatnet = "{c}"; port = "{p}"; autoconnect = "yes"; }}'
                    for c, p in servers)
    net = " ".join(f'{c} = {{ type = "IRC"; }};' for c, _ in servers)
    chn = ", ".join(f'{{ name = "{ch}"; chatnet = "{c}"; autojoin = "yes"; }}'
                    for c, _ in servers for ch in channels)
    return f"""servers = ( {srv} );
chatnets = {{ {net} }};
channels = ( {chn} );
logs = {{ "@HOME@/all.log" = {{ auto_open = "yes"; level = "ALL"; }}; }};
settings = {{
  core = {{ real_name = "live test"; user_name = "{nick}"; nick = "{nick}"; }};
  "fe-common/core" = {{ theme = "default"; autolog = "yes"; autolog_path = "@HOME@/logs/$tag/$0.log"; autolog_level = "ALL"; }};
  "irc/core" = {{ cmd_queue_speed = "5msec"; }};
  "perl/core" = {{ perl_use_lib = "@PERLLIB@"; }};
}};
{extra}
"""


class Erssi:
    """One erssi in a pty. HOME is work/home-<name>, --home is its .erssi."""

    def __init__(self, inst, work, name, config_text=None, args=(), env=None):
        self.inst = inst
        self.work = Path(work).resolve()
        self.name = name
        self.config_text = config_text
        self.args = list(args)
        self.extra_env = dict(env or {})
        self.home_dir = self.work / f"home-{name}"
        self.home = self.home_dir / ".erssi"
        self.proc = None
        self.master = None
        for a in self.args:
            if a in ("-h",) or a.startswith("--home"):
                raise ValueError("the harness sets --home itself")
        self._buf = bytearray()
        self._lock = threading.Lock()
        self._thread = None

    def child_env(self):
        """The environment of the child, built from an allowlist: nothing
        else of the caller's (TMUX, STY, TERM_PROGRAM, IRC*, XDG_*, SSH_*,
        KITTY_*, ...) reaches erssi. The test's env= overrides come last."""
        env = {k: os.environ[k] for k in ("PATH", "PERL5LIB", "LD_LIBRARY_PATH", "LLVM_SYMBOLIZER_PATH",
                                          "ASAN_SYMBOLIZER_PATH") if k in os.environ}
        env.update({
            "HOME": str(self.home_dir),
            "LANG": "C.UTF-8",
            "LC_ALL": "C.UTF-8",
            "TERM": "xterm-256color",
            "ASAN_OPTIONS": f"detect_leaks=0:log_path={self.work}/asan-{self.name}",
            "UBSAN_OPTIONS": f"print_stacktrace=1:log_path={self.work}/ubsan-{self.name}",
        })
        env.update(self.extra_env)
        # the real HOME must never reach a child
        home = Path(env["HOME"]).resolve()
        if not (home == self.work or self.work in home.parents):
            raise AssertionError(f"HOME outside the work dir: {home}")
        return env

    def _prepare(self):
        home = self.home
        for d in ("modules", "logs", "scripts/autorun"):
            (home / d).mkdir(parents=True, exist_ok=True)
        for so in self.inst.moddir.glob("*.so"):
            link = home / "modules" / so.name
            if not link.exists():
                link.symlink_to(so)
        # the theme of this build, from --home: otherwise erssi reads the
        # theme directory of the installation prefix
        theme = self.inst.themedir / "default.theme"
        if theme.is_file():
            shutil.copy(theme, home / "default.theme")
        if self.config_text is not None:
            text = self.config_text.replace("@HOME@", str(home)).replace("@PERLLIB@", str(self.inst.perllib or ""))
            (home / "config").write_text(text)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.kill()

    def start(self, cols=200, rows=50):
        self._prepare()
        env = self.child_env()
        master, slave = os.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

        def ctty():
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        try:
            self.proc = subprocess.Popen([str(self.inst.erssi), f"--home={self.home}"] + self.args,
                                         stdin=slave, stdout=slave, stderr=slave, env=env,
                                         cwd=str(self.home_dir), start_new_session=True, preexec_fn=ctty)
        except BaseException:
            os.close(master)
            raise
        finally:
            os.close(slave)
        self.master = master
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self):
        while True:
            try:
                data = os.read(self.master, 65536)
            except OSError:
                return
            if not data:
                return
            with self._lock:
                self._buf += data

    def type(self, line):
        self.write_raw(line.encode() + b"\r")
        time.sleep(0.3)

    def write_raw(self, data):
        os.write(self.master, data)

    def output(self):
        with self._lock:
            return bytes(self._buf)

    def mark(self):
        with self._lock:
            return len(self._buf)

    def output_since(self, mark):
        with self._lock:
            return bytes(self._buf[mark:])

    def text(self, since=0):
        """The output since a mark() as text, ANSI escape sequences removed."""
        with self._lock:
            raw = bytes(self._buf[since:])
        return ANSI.sub("", raw.decode(errors="replace"))

    def _has(self, pattern):
        with self._lock:
            return pattern in self._buf

    def wait_output(self, pattern, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if self._has(pattern):
                return True
            time.sleep(0.1)
        return self._has(pattern)

    def all_log(self):
        return self.home / "all.log"

    def wait_file(self, path, text, timeout):
        end = time.time() + timeout
        while True:
            try:
                if text in Path(path).read_text(errors="replace"):
                    return True
            except OSError:
                pass
            if time.time() >= end:
                return False
            time.sleep(0.1)

    def quit(self):
        if not self.proc:
            return
        if self.proc.poll() is None:
            try:
                self.type("/quit")
                self.proc.wait(5)
            except (subprocess.TimeoutExpired, OSError):
                pass
        self.kill()

    def kill(self):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except OSError:
                self.proc.kill()
            self.proc.wait()
        # the slave side is closed now: the reader gets EIO and ends. Join it
        # before closing the fd it reads (the number could be reused).
        if self._thread is not None:
            self._thread.join(2)
            self._thread = None
        if self.master is not None:
            try:
                os.close(self.master)
            except OSError:
                pass
            self.master = None

    def sanitizer_reports(self):
        return sorted(p for pat in (f"asan-{self.name}.*", f"ubsan-{self.name}.*") for p in self.work.glob(pat))
