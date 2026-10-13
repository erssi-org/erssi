#!/usr/bin/env python3
"""Write the seed inputs of the e2e fuzz targets into corpus/<target>/.

Seeds are named seed-*; inputs of fixed crashes (crash-*) are added by hand
next to them and never touched here. Deterministic: the wire messages are
fixed ones made by rpe2e.pl (tests/e2e/rpe2e-vectors.h), the keys in the
keyring seeds are made-up bytes, not anybody's keys.
"""

import base64
import hashlib
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def fake(label, n):
    return hashlib.sha256(label.encode()).digest()[:n] if n <= 32 else (
        hashlib.sha512(label.encode()).digest()[:n])


def b64(data):
    return base64.b64encode(data).decode()


def b64u(data):
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


# rpe2e.pl output (see tests/e2e/rpe2e-vectors.h)
WIRE = "+RPE2E01 396b958c840d504a 1791564129 1/1 aoIDyQCW/9kDelxEU66u/tzMJCeVXlPc:Az7K12qPCo84Lrb3+OGd5+vRzSpS"
WIRE_PART = ("+RPE2E01 6ccbf40ba2530bc6 1791564129 4/4 "
             "CGKa/bmEOlIWBrYJK3mqdFhiF7t0vaYq:RZYYDmOq8tuoyMCcxyFDPYASPDg8Ce5ayQU=")
KEYREQ = ("RPEE2E KEYREQ v=1 c=#test p=IVL40Zt5HSRFMkLhXy6rbLfP-ntqXtMAl5YOBpiB2xI "
          "e=dyENFcLDQDPLOFf273NGoG4fNqqFMlJYWm3TBA06Kkk n=COJ6gbQTz82kjdu7MtoJ6g "
          "s=Aa6n-YKBtfjjkGw0OI73ic9lz0wcQuuHPrNwnNnhnwnuMH6eETdRgJZL6gwIFsm4TQB2s-QBcn8szI2cG-_qCQ")


def handshake(kind, ctx="#test", **extra):
    fields = [f"v=1", f"c={ctx}", f"p={b64u(fake('p', 32))}", f"e={b64u(fake('e', 32))}"]
    if kind != "KEYREQ":
        fields += [f"wn={b64u(fake('wn', 24))}", f"w={b64u(fake('w', 48))}"]
    fields += [f"n={b64u(fake('n', 16))}", f"s={b64u(fake('s', 64))}"]
    fields += [f"{k}={v}" for k, v in extra.items()]
    return f"RPEE2E {kind} " + " ".join(fields)


def wire_seeds():
    return {
        "seed-wire": WIRE,
        "seed-wire-part": WIRE_PART,
        "seed-wire-tabs": WIRE.replace(" ", "\t") + "  ",
        "seed-wire-total17": WIRE.replace("1/1", "1/17"),
        "seed-wire-bad-b64": WIRE.replace("Az7K", "Az7*"),
        "seed-wire-huge-ts": WIRE.replace("1791564129", "9" * 30),
        "seed-keyreq": KEYREQ,
        "seed-keyreq-dm": handshake("KEYREQ", "@alice@127.0.0.1"),
        "seed-keyrsp": handshake("KEYRSP"),
        "seed-rekey": handshake("REKEY", "#żaba"),
        "seed-keyreq-dup": handshake("KEYREQ", c="#other"),
        "seed-privmsg": "PRIVMSG #test :hello",
        "seed-privmsg-tags": "@+draft/reply=abc privmsg @+#test :\u0001ACTION waves\u0001",
        "seed-privmsg-bot": "PRIVMSG #test :.op alice",
        "seed-privmsg-multi": "PRIVMSG #a,bob :zażółć gęślą jaźń",
        "seed-utf8-broken": b"za\xc5\xbc\xc3\xb3\xc5\x82\xc4 \xff\xfe g\xc4",
        # the gate folds the target under the server's CASEMAPPING
        "seed-privmsg-unicode-case": "PRIVMSG #Żaba :x",
    }


def keyring_seeds():
    ident = {"pk": b64(fake("pk", 32)), "sk": b64(fake("sk", 64)),
             "fp": fake("fp", 16).hex(), "created_at": 1}
    fp = fake("peer", 16).hex()
    keyring = {
        "identity": ident,
        "peers": {fp: {"pk": b64(fake("peer-pk", 32)), "last_handle": "bob@127.0.0.1",
                       "last_nick": "bob", "first_seen": 1, "last_seen": 2, "status": "trusted"}},
        "outgoing": {"#test": {"sk": b64(fake("out", 32)), "created_at": 1, "pending_rotation": 0}},
        "incoming": {"bob@127.0.0.1|#test": {"fp": fp, "sk": b64(fake("in", 32)),
                                             "status": "trusted", "created_at": 1}},
        "channels": {"#test": {"enabled": 1, "mode": "normal"},
                     "#Å¼aba": {"enabled": 1, "mode": "auto-accept"}},
        "pending": {}, "autotrust": [{"scope": "global", "handle_pattern": "*@*.example.org"}],
        "outgoing_recipients": {"#test|bob@127.0.0.1": {"channel": "#test", "handle": "bob@127.0.0.1",
                                                        "fingerprint": fp, "first_sent_at": 1}},
        "pending_inbound": {}, "pending_trust_change": [],
    }
    export = {
        "version": 1, "exportedAt": 3, "identity": ident,
        "peers": [dict(keyring["peers"][fp], fingerprint=fp)],
        "incomingSessions": [{"handle": "bob@127.0.0.1", "channel": "#test", "fp": fp,
                              "sk": b64(fake("in", 32)), "status": "trusted", "createdAt": 1}],
        "outgoingSessions": [{"channel": "#test", "sk": b64(fake("out", 32)), "pendingRotation": True}],
        "channels": [{"channel": "#test", "enabled": True}],
        "autotrust": [], "outgoingRecipients": [],
    }
    # the module's keyring with the network scope: "<label>\x1f<ctx>"
    sc = "IRCnet\u001f#test"
    req = {"handle": "carol@127.0.0.1", "channel": sc, "sender_nick": "carol",
           "pubkey": b64(fake("carol-pk", 32)), "received_at": 5}
    scoped = dict(keyring,
                  outgoing={sc: {"sk": b64(fake("out", 32)), "created_at": 1, "pending_rotation": 0}},
                  incoming={"bob@127.0.0.1|" + sc: {"fp": fp, "sk": b64(fake("in", 32)),
                                                     "status": "trusted", "created_at": 1}},
                  channels={sc: {"enabled": 1, "mode": "normal"},
                            "IRCnet\u001f@bob@127.0.0.1": {"enabled": 1, "mode": "normal",
                                                           "nick": "bob"}},
                  outgoing_recipients={sc + "|bob@127.0.0.1": {
                      "channel": sc, "handle": "bob@127.0.0.1", "fingerprint": fp,
                      "first_sent_at": 1}},
                  pending_inbound={"carol@127.0.0.1|" + sc: req})
    # old (unscoped) rows next to scoped ones: one name taken over by
    # IRCnet ("adopted"), one old row of unknown network, one scoped to
    # another network, and a name holding the separator twice
    mixed = dict(keyring,
                 channels={"#test": {"enabled": 1, "mode": "normal"},
                           sc: {"enabled": 1, "mode": "normal", "adopted": "IRCnet"},
                           "#old": {"enabled": 1, "mode": "quiet"},
                           "EFnet\u001f#other": {"enabled": 1, "mode": "normal",
                                                 "adopted": "EFnet"},
                           "IRCnet\u001f#a\u001f#b": {"enabled": 1},
                           "#Å¼aba": {"enabled": 0, "mode": "auto-accept"}},
                 outgoing={"#test": {"sk": b64(fake("old-out", 32)), "created_at": 1},
                           sc: {"sk": b64(fake("out", 32)), "created_at": 2, "adopted": "IRCnet"}},
                 incoming={"bob@127.0.0.1|#test": {"fp": fp, "sk": b64(fake("in", 32)),
                                                   "status": "trusted", "created_at": 1},
                           "bob@127.0.0.1|" + sc: {"fp": fp, "sk": b64(fake("in2", 32)),
                                                    "status": "trusted", "created_at": 2},
                           "dave@127.0.0.1|#old": {"fp": fp, "sk": b64(fake("in3", 32)),
                                                   "status": "revoked", "created_at": 1}},
                 pending_inbound={"carol@127.0.0.1|#old": dict(req, channel="#old"),
                                  "carol@127.0.0.1|" + sc: req,
                                  "eve@127.0.0.1|@bob@127.0.0.1": dict(
                                      req, handle="eve@127.0.0.1", channel="@bob@127.0.0.1")})
    return {
        "seed-keyring": json.dumps(keyring, separators=(",", ":"), ensure_ascii=False),
        "seed-keyring-scoped": json.dumps(scoped, indent=3, sort_keys=True),
        "seed-keyring-mixed-legacy": json.dumps(mixed, separators=(",", ":"), sort_keys=True),
        "seed-keyring-ascii": json.dumps(keyring, indent=3, sort_keys=True),
        "seed-export": json.dumps(export, indent=3),
        "seed-export-bad-list": '{"version":1,"peers":{"x":1},"channels":[1,"a",null]}',
        "seed-escapes": '{"a":"\\ud83d\\ude00\\u00e9\\n\\t\\"\\\\\\/","b":[-0.5e-3,0,1e9,true,false,null]}',
        "seed-deep": "{\"a\":" + "[" * 600 + "]" * 600 + "}",
        "seed-surrogate": '{"a":"\\ud800"}',
        "seed-bad-utf8": b'{"a":"\xc3\x28"}',
    }


def write(target, seeds):
    out = os.path.join(HERE, "corpus", target)
    os.makedirs(out, exist_ok=True)
    for name, data in seeds.items():
        if isinstance(data, str):
            data = data.encode()
        with open(os.path.join(out, name), "wb") as fh:
            fh.write(data)


if __name__ == "__main__":
    write("wire", wire_seeds())
    write("keyring", keyring_seeds())
