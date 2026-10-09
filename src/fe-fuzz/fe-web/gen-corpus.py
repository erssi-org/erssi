#!/usr/bin/env python3
"""Write the seed inputs of the fe-web fuzz targets into corpus/<target>/.

Seeds are named seed-*; inputs of fixed crashes (crash-*) are added by hand
next to them and never touched here. Needs python3-cryptography for the
AES-GCM frames. Deterministic: masks and IVs are fixed, so running it again
gives the same files.
"""

import hashlib
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

PASSWORD = b"fuzz-password"  # FE_WEB_FUZZ_PASSWORD in fe-web-fuzz.h
SPLIT = b"@@READ@@"          # FE_WEB_FUZZ_SPLIT
KEY = hashlib.pbkdf2_hmac("sha256", PASSWORD, b"irssi-fe-web-v1", 10000, 32)

HERE = os.path.dirname(os.path.abspath(__file__))


def seal(plain, iv=b"\x01" * 12):
    return iv + AESGCM(KEY).encrypt(iv, plain, None)


def frame(opcode, payload, mask=b"\x11\x22\x33\x44", fin=True, length_form=None):
    head = bytes([(0x80 if fin else 0) | opcode])
    n = len(payload)
    form = length_form or (7 if n < 126 else 16 if n < 65536 else 64)
    bit = 0x80 if mask is not None else 0
    if form == 7:
        head += bytes([bit | n])
    elif form == 16:
        head += bytes([bit | 126]) + struct.pack(">H", n)
    else:
        head += bytes([bit | 127]) + struct.pack(">Q", n)
    if mask is None:
        return head + payload
    return head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


def request(password_in="query", key=b"dGhlIHNhbXBsZSBub25jZQ==", password=PASSWORD,
            key_header=b"Sec-WebSocket-Key"):
    path = b"/"
    extra = b""
    if password_in == "query":
        path = b"/?password=" + password + b"&v=1"
    elif password_in == "bearer":
        extra = b"Authorization: Bearer " + password + b"\r\n"
    return (b"GET " + path + b" HTTP/1.1\r\n"
            b"Host: 127.0.0.1:9001\r\n"
            b"Upgrade: websocket\r\n"
            b"Connection: Upgrade\r\n" + extra +
            key_header + b": " + key + b"\r\n"
            b"Sec-WebSocket-Version: 13\r\n\r\n")


MESSAGES = [
    b'{"type":"ping","id":"1"}',
    b'{"type":"sync_server","server":"*"}',
    b'{"type":"sync_server","server":"fuzz"}',
    b'{"type":"command","command":"/echo hi","server":"fuzz","target":"#chan"}',
    b'{"type":"command","command":"/help"}',
    b'{"type":"close_query","nick":"bob","server":"fuzz"}',
    b'{"type":"names","channel":"#chan","server":"fuzz"}',
    b'{"type":"mark_read","target":"#chan","server":"fuzz"}',
    b'{"type":"network_list","id":"n1"}',
    b'{"type":"server_list","id":"s1","network":"Net"}',
    b'{"type":"network_add","id":"a1","name":"Net","nick":"me","alternate_nick":"me_",'
    b'"username":"u","realname":"Real \\"Name\\" \\u00e9\\n","own_host":"::1",'
    b'"autosendcmd":"/msg x y","usermode":"+i","sasl_mechanism":"PLAIN",'
    b'"sasl_username":"u","sasl_password":"p","max_kicks":4,"max_msgs":3,'
    b'"max_modes":3,"max_whois":1,"max_cmds_at_once":5,"cmd_queue_speed":2200,'
    b'"max_query_chans":1}',
    b'{"type":"network_remove","id":"r1","name":"Net"}',
    b'{"type":"server_add","id":"a2","address":"irc.example.org","port":6697,'
    b'"chatnet":"Net","password":"***","autoconnect":1,"use_tls":1,"tls_verify":0,'
    b'"tls_cert":"c","tls_pkey":"k","tls_cafile":"ca","max_cmds_at_once":5,'
    b'"cmd_queue_speed":2200,"starttls":0,"no_cap":0}',
    b'{"type":"server_add","id":"a3","address":"irc.example.org","password":"secret"}',
    b'{"type":"server_remove","id":"r2","address":"irc.example.org","port":6697,"chatnet":"Net"}',
    b'{"type" : "ping", "id" : "\\ud83d\\ude00 \\/ \\\\ \\b\\f\\r\\t \\u12"}',
    b'{"id":"x","type":"unknown"}',
]

IRC_LINES = [
    b"001 me :Welcome",
    b"005 me CHANTYPES=# PREFIX=(ohv)@%+ CHANMODES=beI,k,l,imnpst :are supported",
    b":me!u@h JOIN :#chan",
    b"353 me = #chan :@me +bob alice",
    b"366 me #chan :End of /NAMES list.",
    b":bob!u@h PRIVMSG #chan :hello me",
    b":bob!u@h PRIVMSG me :private",
    b":bob!u@h PRIVMSG #chan :\x01ACTION waves\x01",
    b":bob!u@h NOTICE #chan :notice",
    b":op!u@h MODE #chan +ov-h+lk bob alice bob 10 key",
    b":op!u@h MODE #chan +b-e *!*@x *!*@y",
    b":op!u@h MODE #chan +nt",
    b":bob!u@h NICK :bobby",
    b":bobby!u@h TOPIC #chan :new topic",
    b"332 me #chan :topic",
    b":op!u@h KICK #chan alice :bye",
    b":bobby!u@h PART #chan :later",
    b":x!u@h QUIT :gone",
    b"311 me bob u h * :Real Name",
    b"312 me bob irc.example.org :Server info",
    b"317 me bob 10 1700000000 :seconds idle, signon time",
    b"319 me bob :@#chan +#other",
    b"330 me bob bobacc :is logged in as",
    b"671 me bob :is using a secure connection",
    b"313 me bob :is an IRC Operator",
    b"301 me bob :away message",
    b"338 me bob 1.2.3.4 :actually using host",
    b"318 me bob :End of /WHOIS list.",
    b"305 me :You are no longer marked as being away",
    b"306 me :You have been marked as being away",
    b":me!u@h MODE me :+iw",
]


def write(target, name, data):
    d = os.path.join(HERE, "corpus", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "seed-" + name), "wb") as f:
        f.write(data)


def main():
    # handshake: anyone who reaches the port
    ping = frame(1, MESSAGES[0])
    write("handshake", "query-password", request("query") + SPLIT + ping)
    write("handshake", "bearer", request("bearer") + SPLIT + ping + frame(8, b"\x03\xe8"))
    write("handshake", "bearer-and-query", request("query").replace(
        b"Upgrade: websocket", b"authorization:\tBearer " + PASSWORD + b"\r\nUpgrade: websocket"))
    write("handshake", "wrong-password", request("query", password=b"guess"))
    write("handshake", "no-password", request(None))
    write("handshake", "lowercase-key", request("bearer", key_header=b"sec-websocket-key"))
    write("handshake", "split-request", request("bearer")[:40] + SPLIT + request("bearer")[40:])
    write("handshake", "no-key", b"GET /?password=fuzz-password HTTP/1.1\r\nHost: x\r\n\r\n")
    write("handshake", "frames-in-same-read", request("query") + ping)
    write("handshake", "encrypted-after", request("bearer") + SPLIT +
          frame(2, seal(MESSAGES[1])))
    write("handshake", "garbage", b"\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03")

    # frames: after login
    for i, msg in enumerate(MESSAGES):
        write("frames", "text-%02d" % i, frame(1, msg, mask=bytes([i, 0x5a, 0xa5, 0xff])))
        write("frames", "binary-%02d" % i, frame(2, seal(msg, iv=bytes([i]) * 12)))
    write("frames", "len16", frame(1, b'{"type":"ping","id":"' + b"x" * 300 + b'"}'))
    write("frames", "len64", frame(1, b'{"type":"ping"}', length_form=64))
    write("frames", "len64-huge", b"\x81\xff" + struct.pack(">Q", 1 << 63) + b"\0\0\0\0")
    write("frames", "unmasked-text", frame(1, MESSAGES[0], mask=None))
    write("frames", "ping", frame(9, b"are you there"))
    write("frames", "ping-empty", frame(9, b""))
    write("frames", "pong", frame(10, b"x"))
    write("frames", "close-status", frame(8, b"\x03\xe9going away"))
    write("frames", "close-empty", frame(8, b""))
    write("frames", "close-1byte", frame(8, b"\x03"))
    write("frames", "continuation", frame(1, b'{"type":', fin=False) + frame(0, b'"ping"}'))
    write("frames", "two-in-one-read", ping + frame(1, MESSAGES[1]))
    write("frames", "split-over-reads", ping[:3] + SPLIT + ping[3:9] + SPLIT + ping[9:])
    write("frames", "binary-short", frame(2, b"\x00" * 27))
    write("frames", "binary-bad-tag", frame(2, seal(MESSAGES[0])[:-1] + b"\x00"))

    # message: the JSON alone
    for i, msg in enumerate(MESSAGES):
        write("message", "%02d" % i, msg)

    # crypto: binary frame payloads
    write("crypto", "valid", seal(MESSAGES[0]))
    write("crypto", "valid-empty", seal(b""))
    write("crypto", "short", b"\x00" * 27)
    write("crypto", "iv-and-tag", b"\x00" * 28)

    # irc: server lines, and "WEB " lines from the web client
    web = [b'WEB {"type":"sync_server","server":"*"}',
           b'WEB {"type":"names","channel":"#chan","server":"fuzz"}',
           b'WEB {"type":"mark_read","target":"#chan","server":"fuzz"}',
           b'WEB {"type":"mark_read","target":"bob","server":"fuzz"}',
           b'WEB {"type":"close_query","nick":"bob","server":"fuzz"}',
           b'WEB {"type":"sync_server","server":"fuzz"}']
    write("irc", "session", b"\x00" + b"\r\n".join(IRC_LINES[:5] + web[:1] + IRC_LINES[5:] + web[1:]))
    write("irc", "prefixed", b"\x01" + b"\r\n".join([b"PRIVMSG #chan :hi", b"MODE #chan +o x"]))
    write("irc", "whois", b"\x00" + b"\r\n".join(IRC_LINES[:1] + IRC_LINES[18:28]))


if __name__ == "__main__":
    sys.exit(main())
