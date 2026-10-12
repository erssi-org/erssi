#!/usr/bin/env python3
"""A minimal IRC server for testing rpe2e.pl between two erssi clients.

127.0.0.1 only. It handles what erssi and rpe2e.pl need: registration
(CAP/NICK/USER), PING, JOIN with NAMES, MODE, TOPIC, PART, nick changes, WHO,
WHOIS (311 - rpe2e.pl learns its own ident@host from it), PRIVMSG/NOTICE to a
channel and to a nick (CTCP too, which carries KEYREQ/KEYRSP). Every
PRIVMSG/NOTICE line is also written to the --wire file, so the test can check
what really crossed the server; --wire-times writes them once more with the
time they arrived ("<seconds.ms> <nick> <CMD> <target> :<text>"), so the
test can check the pace of the flood queue. --network names the network in 005 NETWORK=,
so two of these can play two networks. --casemapping ascii, rfc1459,
strict-rfc1459 or utf8 sends CASEMAPPING=<value> in 005 and folds channel
and nick names that way (utf8: NFKC, then Python's casefold); without it
no CASEMAPPING is sent and names are folded with str.lower().
--hide-casemapping folds as --casemapping says but names none in 005.
"""
import argparse
import asyncio
import time
import unicodedata

SRV = "e2e.test"
NETWORK = "E2ETest"
CASEMAPPING = None
HIDE_CASEMAPPING = False
clients = {}   # nick (folded) -> Client
channels = {}  # channel (folded) -> set(nick folded)

RFC1459_UPPER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ[]\\~", "abcdefghijklmnopqrstuvwxyz{}|^")
STRICT_UPPER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ[]\\", "abcdefghijklmnopqrstuvwxyz{}|")
ASCII_UPPER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")


def fold(name):
    """a channel or nick name as this server compares it"""
    if CASEMAPPING == "utf8":
        return unicodedata.normalize("NFKC", name).casefold()
    if CASEMAPPING == "rfc1459":
        return name.translate(RFC1459_UPPER)
    if CASEMAPPING == "strict-rfc1459":
        return name.translate(STRICT_UPPER)
    if CASEMAPPING == "ascii":
        return name.translate(ASCII_UPPER)
    return name.lower()


class Client:
    def __init__(self, writer):
        self.writer = writer
        self.nick = "*"
        self.user = "u"

    @property
    def prefix(self):
        return f"{self.nick}!{self.user}@127.0.0.1"

    def send(self, line):
        self.writer.write((line + "\r\n").encode())


def members(chan):
    return [clients[n] for n in channels.get(fold(chan), ()) if n in clients]


async def handle(reader, writer, wire_path, times_path):
    me = Client(writer)
    try:
        while line := await reader.readline():
            text = line.decode(errors="replace").rstrip("\r\n")
            if not text:
                continue
            parts = text.split(" ")
            cmd, args = parts[0].upper(), parts[1:]
            trailing = text.split(" :", 1)[1] if " :" in text else (args[-1] if args else "")
            if cmd == "CAP" and args and args[0] == "LS":
                me.send(f":{SRV} CAP * LS :")
            elif cmd == "NICK":
                new = args[0].lstrip(":")
                if me.nick != "*":
                    seen = {me}
                    for chan, nicks in channels.items():
                        if fold(me.nick) in nicks:
                            nicks.discard(fold(me.nick))
                            nicks.add(fold(new))
                            for other in [clients[n] for n in nicks if n in clients]:
                                seen.add(other)
                    for c in seen:
                        c.send(f":{me.prefix} NICK :{new}")
                    clients.pop(fold(me.nick), None)
                me.nick = new
                clients[fold(me.nick)] = me
            elif cmd == "USER":
                me.user = args[0]
                for num, msg in (("001", f"Welcome {me.nick}"), ("002", f"Your host is {SRV}"), ("003", "today"),
                                 ("004", f"{SRV} e2e io biklmnopstv")):
                    me.send(f":{SRV} {num} {me.nick} :{msg}")
                cm = f" CASEMAPPING={CASEMAPPING}" if CASEMAPPING and not HIDE_CASEMAPPING else ""
                me.send(f":{SRV} 005 {me.nick} CHANTYPES=# PREFIX=(ov)@+ NETWORK={NETWORK}{cm} :are supported")
                me.send(f":{SRV} 376 {me.nick} :End of MOTD")
            elif cmd == "PING":
                me.send(f":{SRV} PONG {SRV} :{args[0].lstrip(':') if args else ''}")
            elif cmd == "JOIN":
                for chan in args[0].split(","):
                    channels.setdefault(fold(chan), set()).add(fold(me.nick))
                    for other in members(chan):
                        other.send(f":{me.prefix} JOIN {chan}")
                    names = " ".join(c.nick for c in members(chan))
                    me.send(f":{SRV} 353 {me.nick} = {chan} :{names}")
                    me.send(f":{SRV} 366 {me.nick} {chan} :End of NAMES")
            elif cmd == "MODE" and args and args[0].startswith("#"):
                if len(args) == 1:
                    me.send(f":{SRV} 324 {me.nick} {args[0]} +nt")
                elif len(args) == 2 and args[1].lstrip("+") in ("b", "e", "I"):
                    end = {"b": "368", "e": "349", "I": "347"}[args[1].lstrip("+")]
                    me.send(f":{SRV} {end} {me.nick} {args[0]} :End of list")
                else:
                    for c in members(args[0]):
                        c.send(f":{me.prefix} MODE {' '.join(args)}")
            elif cmd == "TOPIC" and len(args) >= 2:
                for c in members(args[0]):
                    c.send(f":{me.prefix} TOPIC {args[0]} :{trailing}")
            elif cmd == "PART" and args:
                for chan in args[0].split(","):
                    for c in members(chan):
                        c.send(f":{me.prefix} PART {chan} :{trailing if len(args) > 1 else ''}")
                    channels.get(fold(chan), set()).discard(fold(me.nick))
            elif cmd == "WHO" and args:
                for c in members(args[0]):
                    me.send(f":{SRV} 352 {me.nick} {args[0]} {c.user} 127.0.0.1 {SRV} {c.nick} H :0 {c.nick}")
                me.send(f":{SRV} 315 {me.nick} {args[0]} :End of WHO")
            elif cmd == "WHOIS" and args:
                target = clients.get(fold(args[-1]))
                if target:
                    me.send(f":{SRV} 311 {me.nick} {target.nick} {target.user} 127.0.0.1 * :{target.nick}")
                me.send(f":{SRV} 318 {me.nick} {args[-1]} :End of WHOIS")
            elif cmd in ("PRIVMSG", "NOTICE") and len(args) >= 2:
                target = args[0]
                with open(wire_path, "a", encoding="utf-8") as fh:
                    fh.write(f"{me.nick} {cmd} {target} :{trailing}\n")
                if times_path:
                    with open(times_path, "a", encoding="utf-8") as fh:
                        fh.write(f"{time.time():.3f} {me.nick} {cmd} {target} :{trailing}\n")
                out = f":{me.prefix} {cmd} {target} :{trailing}"
                if target.startswith("#"):
                    for c in members(target):
                        if c is not me:
                            c.send(out)
                elif fold(target) in clients:
                    clients[fold(target)].send(out)
            elif cmd == "QUIT":
                break
            await writer.drain()
    finally:
        clients.pop(fold(me.nick), None)
        for chan in channels.values():
            chan.discard(fold(me.nick))
        writer.close()


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--wire", required=True)
    parser.add_argument("--wire-times", help="every PRIVMSG/NOTICE again, with the time it arrived")
    parser.add_argument("--network", default="E2ETest")
    parser.add_argument("--casemapping", choices=("ascii", "rfc1459", "strict-rfc1459", "utf8"))
    parser.add_argument("--hide-casemapping", action="store_true",
                        help="fold as --casemapping says, but name no CASEMAPPING in 005")
    args = parser.parse_args()
    global NETWORK, CASEMAPPING, HIDE_CASEMAPPING
    NETWORK = args.network
    CASEMAPPING = args.casemapping
    HIDE_CASEMAPPING = args.hide_casemapping
    server = await asyncio.start_server(lambda r, w: handle(r, w, args.wire, args.wire_times), "127.0.0.1", args.port)
    async with server:
        await server.serve_forever()


asyncio.run(main())
