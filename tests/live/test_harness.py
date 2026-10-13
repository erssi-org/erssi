#!/usr/bin/env python3
"""Tests of the live-test harness: erssi in a pty, a fake IRC server, a raw
client, TESTRAW, a hermetic environment.
Usage: test_harness.py <build dir> [<source dir>]"""
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402

# set in the harness process; none of them may reach erssi
LEAKY = {"TMUX": "/nonexistent/tmux-sock,1,0", "STY": "1.leak", "TERM_PROGRAM": "leakterm",
         "LC_TERMINAL": "leakterm", "IRCNICK": "leaknick", "XDG_RUNTIME_DIR": "/nonexistent",
         "SSH_AUTH_SOCK": "/nonexistent", "KITTY_PID": "1", "WEZTERM_PANE": "1",
         "GHOSTTY_RESOURCES_DIR": "/nonexistent", "LANGUAGE": "xx"}


def main():
    build = Path(sys.argv[1]).resolve()
    work = Path(tempfile.mkdtemp(prefix="live-harness."))
    inst = None

    def t_install():
        nonlocal inst
        inst = harness.install(build, work)
        assert inst.erssi.is_file() and inst.moddir.is_dir()

    def t_ircd_client():
        (work / "ircd-only").mkdir()
        with harness.Ircd(work / "ircd-only") as ircd:
            port = ircd.start()
            with harness.Client(port, "carol") as c:
                c.send("PING :x")
                c.expect("PONG")
                c.send("FROBNICATE")
                c.expect(" 421 ")

    def t_hermetic_env():
        e = harness.Erssi(inst, work, "iso", harness.config(1, "iso"), env={"TERM": "vt100", "LIVE_MARK": "1"})
        assert e.home.is_relative_to(work), e.home
        saved = {k: os.environ.get(k) for k in LEAKY}
        os.environ.update(LEAKY)
        try:
            env = e.child_env()
            assert Path(env["HOME"]).is_relative_to(work)
            assert not set(env) & set(LEAKY), set(env) & set(LEAKY)
            assert env["TERM"] == "vt100" and env["LIVE_MARK"] == "1"
            # the real thing: the environment of a running erssi
            (work / "leakcheck").mkdir()
            with harness.Erssi(inst, work / "leakcheck", "leak", harness.config(1, "leak")) as live:
                live.start()
                raw = Path(f"/proc/{live.proc.pid}/environ").read_bytes().split(b"\0")
                names = {x.split(b"=", 1)[0].decode() for x in raw if x}
                assert not names & set(LEAKY), names & set(LEAKY)
                assert f"HOME={live.home_dir}".encode() in raw
        finally:
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v

    def t_smoke():
        w = Path(tempfile.mkdtemp(prefix="smoke.", dir=work))
        with harness.Ircd(w) as ircd, harness.Erssi(inst, w, "alice", None) as alice:
            port = ircd.start()
            alice.config_text = harness.config(port, "alice")
            alice.start()
            with harness.Client(port, "bob") as bob:
                # the join waits are what makes the test deterministic
                assert alice.wait_file(alice.all_log(), "has joined #test", 15), "alice never joined #test"
                bob.send("JOIN #test")
                assert alice.wait_file(alice.all_log(), "bob [u@127.0.0.1] has joined #test", 10), \
                    "alice did not see bob join"
                bob.send("PRIVMSG #test :smoke hello")
                assert alice.wait_file(alice.home / "logs/E2E/#test.log", "smoke hello", 15), \
                    "channel message not logged"
                # /echo does not expand variables, an alias does: the line must
                # hold the name the active window was just given
                alice.type("/window name livewin")
                alice.type("/alias wn echo active=$winname")
                alice.type("/wn")
                assert alice.wait_file(alice.all_log(), "active=livewin", 5), "$winname not expanded"
                bob.send("TESTRAW alice ::e2e.test NOTICE alice :raw works")
                assert alice.wait_file(alice.all_log(), "raw works", 5), "TESTRAW line not delivered"
            alice.quit()
            assert alice.sanitizer_reports() == [], alice.sanitizer_reports()

    try:
        harness.main([t_install, t_ircd_client, t_hermetic_env, t_smoke])
    finally:
        if not harness.keep_work():
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
