#!/usr/bin/env python3
"""The native Mentions window: private messages, private actions, notices,
DCC CHAT, the 0600 log, /MENTIONS and the window's life cycle.
Usage: test_mentions.py <build dir> [<source dir>]"""
import os
import re
import shutil
import socket
import stat
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402

WAIT = 10


def main():
    build = Path(sys.argv[1]).resolve()
    work = Path(tempfile.mkdtemp(prefix="live-mentions."))
    st = {}

    def log_size():
        try:
            return st["alice"].all_log().stat().st_size
        except OSError:
            return 0

    def log_since(offset):
        return st["alice"].all_log().read_bytes()[offset:].decode(errors="replace")

    def wait_log_since(offset, text, timeout=WAIT):
        end = time.time() + timeout
        while True:
            if text in log_since(offset):
                return True
            if time.time() >= end:
                return False
            time.sleep(0.1)

    def wait_log_re(pattern, offset=0, timeout=WAIT):
        end = time.time() + timeout
        while True:
            if re.search(pattern, log_since(offset)):
                return True
            if time.time() >= end:
                return False
            time.sleep(0.1)

    def ml_text():
        try:
            return st["ML"].read_bytes().decode(errors="replace")
        except OSError:
            return ""

    def wait_ml(text):
        return st["alice"].wait_file(st["ML"], text, WAIT)

    def absent_ml(*texts):
        time.sleep(2)
        content = ml_text()
        for t in texts:
            assert t not in content, f"{t!r} is in the mentions log:\n{content}"

    def active_name():
        """$winname of the active window, read through an alias (/echo does
        not expand variables)."""
        off = log_size()
        st["alice"].type("/wn")
        assert wait_log_since(off, "active="), "no active= line"
        m = re.findall(r"active=(\S*)", log_since(off))
        return m[-1]

    def t_install():
        st["inst"] = harness.install(build, work)

    def t_start():
        ircd = harness.Ircd(work)
        st["ircd"] = ircd
        port = ircd.start()
        st["port"] = port
        cfg = harness.config(port, "alice", servers=[("E2E", port), ("Bitlbee", port)])
        cfg = cfg.replace('Bitlbee = { type = "IRC"; };', 'Bitlbee = { type = "IRC"; nick = "alicebot"; };')
        assert 'nick = "alicebot"' in cfg, cfg
        alice = harness.Erssi(st["inst"], work, "alice", cfg)
        st["alice"] = alice
        st["ML"] = alice.home / "logs" / "mentions.log"
        alice.start()
        assert alice.wait_file(alice.all_log(), "has joined #test", 15), "alice never joined #test"
        st["bob"] = bob = harness.Client(port, "bob")
        st["carol"] = carol = harness.Client(port, "carol")
        bob.send("JOIN #test")
        carol.send("JOIN #test")
        assert alice.wait_file(alice.all_log(), "carol [u@127.0.0.1] has joined #test", WAIT), \
            "alice did not see carol join"
        alice.type("/alias wn echo active=$winname")
        alice.type("/set mentions_log on")
        assert wait_log_re(r"mentions_log\s+(= )?ON"), "mentions_log not set"

    def t_window_created_not_active():
        off = log_size()
        st["alice"].type("/window list")
        assert wait_log_since(off, "Mentions"), "no Mentions window in /window list:\n" + log_since(off)
        name = active_name()
        assert name != "Mentions", "the Mentions window was made active"

    def t_private():
        st["bob"].send("PRIVMSG alice :private hello")
        assert wait_ml("[E2E] PM <bob> private hello"), ml_text()
        assert st["alice"].wait_file(st["alice"].all_log(), "PM bob: private hello", WAIT), \
            "no PM line in the Mentions window"

    def t_private_action():
        st["bob"].send("PRIVMSG alice :\x01ACTION dances\x01")
        assert wait_ml("[E2E] PM <bob> dances"), ml_text()

    def t_notice_from_person():
        st["bob"].send("NOTICE alice :psst")
        assert wait_ml("[E2E] NOTICE <bob> psst"), ml_text()

    def t_notice_from_server_and_channel_skipped():
        st["bob"].send("TESTRAW alice ::e2e.test NOTICE alice :server says hi")
        st["bob"].send("NOTICE #test :to channel")
        log = st["alice"].all_log()
        assert st["alice"].wait_file(log, "server says hi", WAIT), "server notice not delivered"
        assert st["alice"].wait_file(log, "to channel", WAIT), "channel notice not delivered"
        absent_ml("server says hi", "to channel")

    def t_own_and_echo():
        st["bob"].send("TESTRAW alice ::alice!u@127.0.0.1 PRIVMSG bob :echoed own")
        assert st["alice"].wait_file(st["alice"].all_log(), "echoed own", WAIT), "echo not delivered"
        absent_ml("echoed own")

    def t_format_injection():
        # remote text must not be read as format codes or theme variables
        text = "fmt %n$0{ %_x$1 $winname %%"
        st["bob"].send("PRIVMSG alice :" + text)
        assert wait_ml("[E2E] PM <bob> " + text + "\n"), ml_text()
        assert st["alice"].wait_file(st["alice"].all_log(), "PM bob: " + text, WAIT), \
            "the Mentions line does not show the text as it was sent"

    def t_ignore():
        alice = st["alice"]
        alice.type("/ignore bob NOTICES ACTIONS")
        st["bob"].send("NOTICE alice :ignored notice")
        st["bob"].send("PRIVMSG alice :\x01ACTION ignored act\x01")
        absent_ml("ignored notice", "ignored act")
        alice.type("/unignore bob")
        st["bob"].send("NOTICE alice :after unignore")
        assert wait_ml("[E2E] NOTICE <bob> after unignore"), ml_text()

    def t_ignore_nohilight():
        # a flag ignore (NOHILIGHT) only takes the hilight away: the line is
        # still shown, but it is not a mention (as in a channel)
        alice = st["alice"]
        carol = st["carol"]
        off = log_size()
        alice.type("/ignore carol MSGS NOTICES ACTIONS NOHILIGHT")
        assert wait_log_since(off, "Ignoring"), log_since(off)
        carol.send("PRIVMSG alice :nohilight pm")
        carol.send("NOTICE alice :nohilight notice")
        carol.send("PRIVMSG alice :\x01ACTION nohilight act\x01")
        for text in ("nohilight pm", "nohilight notice", "nohilight act"):
            assert alice.wait_file(alice.all_log(), text, WAIT), f"{text!r} not delivered"
        absent_ml("nohilight pm", "nohilight notice", "nohilight act")
        off = log_size()
        alice.type("/unignore 1")
        assert wait_log_since(off, "Unignored"), log_since(off)
        carol.send("PRIVMSG alice :after nohilight")
        assert wait_ml("[E2E] PM <carol> after nohilight"), ml_text()

    def t_mask_targets():
        # a NOTICE or PRIVMSG to a server or host mask, or a NOTICE to
        # another nick, is not written to you
        alice = st["alice"]
        bob = st["bob"]
        bob.send("TESTRAW alice ::bob!u@127.0.0.1 NOTICE $*.example.net :mask notice")
        bob.send("TESTRAW alice ::bob!u@127.0.0.1 PRIVMSG $*.example.net :mask privmsg")
        bob.send("TESTRAW alice ::bob!u@127.0.0.1 NOTICE dave :notice for dave")
        for text in ("mask notice", "mask privmsg", "notice for dave"):
            assert alice.wait_file(alice.all_log(), text, WAIT), f"{text!r} not delivered"
        bob.send("NOTICE alice :after masks")
        assert wait_ml("[E2E] NOTICE <bob> after masks"), ml_text()
        absent_ml("mask notice", "mask privmsg", "notice for dave")

    def t_cnotice_ignore():
        # a NOTICE with a [#channel] context is checked against the ignores of
        # that channel, as erssi does when it prints it
        alice = st["alice"]
        off = log_size()
        alice.type("/ignore -channels #test bob PUBNOTICES")
        assert wait_log_since(off, "Ignoring"), log_since(off)
        st["bob"].send("NOTICE alice :[#test] cnotice ignored")
        st["bob"].send("NOTICE alice :control after cnotice")
        assert wait_ml("[E2E] NOTICE <bob> control after cnotice"), ml_text()
        absent_ml("cnotice ignored")
        off = log_size()
        alice.type("/unignore 1")
        assert wait_log_since(off, "Unignored"), log_since(off)
        st["bob"].send("NOTICE alice :[#test] cnotice shown")
        assert wait_ml("[E2E] NOTICE <bob> [#test] cnotice shown"), ml_text()

    def t_excluded_network():
        alice = st["alice"]
        off = log_size()
        alice.type("/set mentions_exclude_networks Bitlbee")
        assert wait_log_re(r"mentions_exclude_networks\s+(= )?Bitlbee", off), "setting not changed"
        st["carol"].send("PRIVMSG alicebot :relay text")
        assert alice.wait_file(alice.all_log(), "relay text", WAIT), "alicebot did not get it"
        absent_ml("relay text")
        st["carol"].send("PRIVMSG alice :normal text")
        assert wait_ml("[E2E] PM <carol> normal text"), ml_text()

    def t_utf8_bytes():
        st["bob"].send("PRIVMSG alice :zażółć gęślą jaźń")
        want = b"<bob> za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84\n"
        end = time.time() + WAIT
        while want not in st["ML"].read_bytes() and time.time() < end:
            time.sleep(0.1)
        assert want in st["ML"].read_bytes(), st["ML"].read_bytes()

    def t_log_modes():
        alice = st["alice"]
        path = work / "new" / "sub" / "m.log"
        alice.type(f"/set mentions_log_file {path}")
        st["bob"].send("PRIVMSG alice :to the new log")
        assert alice.wait_file(path, "<bob> to the new log", WAIT), "new log file not written"
        assert stat.S_IMODE(path.stat().st_mode) == 0o600, oct(path.stat().st_mode)
        assert stat.S_IMODE(path.parent.stat().st_mode) == 0o700, oct(path.parent.stat().st_mode)

    def t_log_error_once():
        alice = st["alice"]
        off = log_size()
        alice.type(f"/set mentions_log_file {work}")
        st["bob"].send("PRIVMSG alice :log error one")
        st["bob"].send("PRIVMSG alice :log error two")
        assert wait_log_since(off, "PM bob: log error two"), "second PM not in the window"
        time.sleep(1)
        n = log_since(off).count("Mentions: cannot write")
        assert n == 1, f"{n} error lines:\n" + log_since(off)
        assert f"Mentions: cannot write {work}: Is a directory" in log_since(off), log_since(off)
        alice.type("/set -default mentions_log_file")
        st["bob"].send("PRIVMSG alice :log works again")
        assert wait_ml("[E2E] PM <bob> log works again"), ml_text()
        # a successful write resets it: the same bad path is reported again
        off = log_size()
        alice.type(f"/set mentions_log_file {work}")
        st["bob"].send("PRIVMSG alice :log error three")
        assert wait_log_since(off, "Mentions: cannot write"), "no error after a good write:\n" + log_since(off)
        alice.type("/set -default mentions_log_file")
        st["bob"].send("PRIVMSG alice :log works once more")
        assert wait_ml("[E2E] PM <bob> log works once more"), ml_text()

    def t_dcc():
        alice = st["alice"]
        lsock = socket.socket()
        lsock.bind(("127.0.0.1", 0))
        lsock.listen(1)
        lsock.settimeout(WAIT)
        port = lsock.getsockname()[1]
        conn = None
        try:
            st["bob"].send(f"PRIVMSG alice :\x01DCC CHAT chat 2130706433 {port}\x01")
            assert alice.wait_file(alice.all_log(), "DCC CHAT from bob", WAIT), "no DCC CHAT request"
            alice.type("/dcc chat bob")
            conn, _ = lsock.accept()
            conn.sendall(b"dcc hello\n")
            assert wait_ml("[DCC] DCC <bob> dcc hello"), ml_text()
            assert alice.wait_file(alice.all_log(), "DCC bob: dcc hello", WAIT), "no DCC line in the window"
        finally:
            if conn is not None:
                conn.close()
            lsock.close()

    def t_dcc_excluded_network():
        # a DCC CHAT that came through an excluded network (Bitlbee) is left out.
        # The offer is from carol: the chat with bob of t_dcc may still be
        # on its way out, and a second "/dcc chat bob" would race with it.
        alice = st["alice"]
        lsock = socket.socket()
        lsock.bind(("127.0.0.1", 0))
        lsock.listen(1)
        lsock.settimeout(WAIT)
        port = lsock.getsockname()[1]
        conn = None
        try:
            off = log_size()
            st["carol"].send(f"PRIVMSG alicebot :\x01DCC CHAT chat 2130706433 {port}\x01")
            assert wait_log_since(off, "DCC CHAT from carol"), "no DCC CHAT request:\n" + log_since(off)
            alice.type("/dcc chat carol")
            conn, _ = lsock.accept()
            conn.sendall(b"excluded dcc\n")
            assert wait_log_since(off, "excluded dcc"), "DCC text not shown:\n" + log_since(off)
            absent_ml("excluded dcc")
            assert "DCC carol: excluded dcc" not in log_since(off), "printed to the Mentions window"
        finally:
            if conn is not None:
                conn.close()
            lsock.close()

    def t_command():
        alice = st["alice"]
        alice.type("/mentions")
        assert active_name() == "Mentions", "/mentions did not go to the window"
        off = log_size()
        alice.type("/mentions clear")
        time.sleep(1)
        text = log_since(off)
        assert "Unknown command" not in text and "rror" not in text, text
        if st["inst"].has_perl:
            off = log_size()
            alice.type('/script exec use Irssi::TextUI; Irssi::print("lines=".'
                       'Irssi::window_find_name("Mentions")->view->{buffer}{lines_count})')
            assert wait_log_since(off, "lines="), "script exec printed nothing:\n" + log_since(off)
            assert "lines=0" in log_since(off), log_since(off)
        alice.type("/window goto 1")

    def t_immortal_and_recreated():
        alice = st["alice"]
        refused = "Window is immortal"
        off = log_size()
        alice.type("/window goto Mentions")
        alice.type("/window close")
        assert wait_log_since(off, refused), "the window is not immortal:\n" + log_since(off)
        alice.type("/window immortal off")
        assert wait_log_since(off, "Window isn't immortal"), log_since(off)
        off = log_size()
        alice.type("/window close")
        time.sleep(1)
        assert refused not in log_since(off), "the close was refused:\n" + log_since(off)
        off = log_size()
        alice.type("/window list")
        time.sleep(1)
        assert re.search(r"\bMentions\b", log_since(off)), "not recreated:\n" + log_since(off)
        assert active_name() != "Mentions", "the recreated window was made active"
        st["bob"].send("PRIVMSG alice :after recreate")
        assert alice.wait_file(alice.all_log(), "PM bob: after recreate", WAIT), \
            "the recreated window gets no entries"
        # only a new window is immortal again after IMMORTAL OFF
        off = log_size()
        alice.type("/window goto Mentions")
        alice.type("/window close")
        assert wait_log_since(off, refused), "the recreated window is not immortal:\n" + log_since(off)
        alice.type("/window goto 1")

    def t_rename():
        alice = st["alice"]
        alice.type("/set mentions_window Inbox")
        off = log_size()
        alice.type("/window list")
        assert wait_log_re(r"\bInbox\b", off), "not renamed:\n" + log_since(off)
        assert not re.search(r"\bMentions\b", log_since(off)), "a second window:\n" + log_since(off)
        st["bob"].send("PRIVMSG alice :renamed window")
        assert wait_ml("[E2E] PM <bob> renamed window"), ml_text()
        alice.type("/set mentions_window Mentions")
        off = log_size()
        alice.type("/window list")
        assert wait_log_re(r"\bMentions\b", off), "not renamed back:\n" + log_since(off)
        assert not re.search(r"\bInbox\b", log_since(off)), "Inbox left over:\n" + log_since(off)

    def t_name_taken():
        # a name another window has is refused: no two windows of one name
        alice = st["alice"]
        alice.type("/window new hidden")
        alice.type("/window name Other")
        alice.type("/window goto 1")
        off = log_size()
        alice.type("/set mentions_window Other")
        assert wait_log_since(off, "Mentions: there is a window named Other already"), log_since(off)
        assert wait_log_re(r"mentions_window\s+(= )?Mentions", off), "the old name was not kept"
        off = log_size()
        alice.type("/window list")
        time.sleep(1)
        listing = log_since(off)
        assert len(re.findall(r"\bOther\b", listing)) == 1, listing
        assert re.search(r"\bMentions\b", listing), listing
        st["bob"].send("PRIVMSG alice :name kept")
        assert alice.wait_file(alice.all_log(), "PM bob: name kept", WAIT), "the window stopped working"
        alice.type("/window goto Other")
        alice.type("/window close")
        alice.type("/window goto 1")

    def t_window_off():
        alice = st["alice"]
        alice.type("/set -clear mentions_window")
        # the window exists now: the "closes and stays closed" checks below
        # would pass vacuously if /window goto failed to find it
        off = log_size()
        alice.type("/window list")
        assert wait_log_since(off, "Mentions"), "no Mentions window before the check:\n" + log_since(off)
        off = log_size()
        alice.type("/mentions")
        assert wait_log_since(off, "The Mentions window is off"), log_since(off)
        st["bob"].send("PRIVMSG alice :window is off")
        assert wait_ml("[E2E] PM <bob> window is off"), ml_text()
        time.sleep(1)
        assert "PM bob: window is off" not in log_since(off), "printed to a window that is off"
        # the old window is an ordinary one now: it closes and stays closed
        off = log_size()
        alice.type("/window goto Mentions")
        alice.type("/window close")
        time.sleep(1)
        assert "Window is immortal" not in log_since(off), "still immortal:\n" + log_since(off)
        off = log_size()
        alice.type("/window list")
        time.sleep(1)
        assert not re.search(r"\bMentions\b", log_since(off)), "made again:\n" + log_since(off)

    def t_help():
        alice = st["alice"]
        alice.type(f"/set help_path {st['inst'].helpdir}")
        off = log_size()
        alice.type("/help mentions")
        assert wait_log_since(off, "MENTIONS CLEAR"), "/help mentions:\n" + log_since(off)
        # the settings are documented too
        assert wait_log_since(off, "mentions_exclude_networks"), "/help mentions:\n" + log_since(off)

    def t_sanitizers():
        alice = st["alice"]
        alice.quit()
        assert alice.sanitizer_reports() == [], alice.sanitizer_reports()

    try:
        harness.main([t_install, t_start, t_window_created_not_active, t_private, t_private_action,
                      t_notice_from_person, t_notice_from_server_and_channel_skipped, t_own_and_echo,
                      t_format_injection, t_ignore, t_ignore_nohilight, t_mask_targets, t_cnotice_ignore,
                      t_excluded_network, t_utf8_bytes, t_log_modes,
                      t_log_error_once, t_dcc, t_dcc_excluded_network, t_command, t_immortal_and_recreated,
                      t_rename, t_name_taken, t_window_off,
                      t_help, t_sanitizers])
    finally:
        for name in ("bob", "carol"):
            if name in st:
                st[name].close()
        if "alice" in st:
            st["alice"].kill()
        if "ircd" in st:
            st["ircd"].stop()
        if not harness.keep_work():
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
