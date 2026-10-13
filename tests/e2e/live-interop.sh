#!/usr/bin/env bash
# live-interop.sh - the native e2e module against rpe2e.pl, live
#
#   live-interop.sh <meson build dir> <source dir>
#
# Two erssi clients of this build on a local fake IRC server
# (tests/e2e/e2eircd.py, 127.0.0.1 only):
#   alice - the NATIVE module (src/e2e),
#   bob   - the rpe2e.pl script from repartee ($RPE2E_SCRIPT),
#           with the native module loaded as well: it must notice the
#           script and stay out of the way. Repartee's 0.2.0 needs two
#           changes for this: its version check refuses erssi (it wants
#           irssi 1.4.1 or newer by $J, erssi reports 1.3.x), and it
#           encodes the UTF-8 bytes irssi hands it a second time, so the
#           other side reads non-ASCII text garbled (4 checks fail).
# They exchange keys and messages in both directions on a channel and in a
# query: plain text, Polish text, a long message that is chunked, /me,
# "..." and /quote privmsg. Checked: each side reads the other in plain
# text (erssi logs), and the server never saw any of it in plain text.
# In normal mode each side accepts the other once (/e2e accept).
#
# A third client, mallory (native module, auto-accept), plays the outsider
# of the trust rules: his messages make alice ask him for his key, but
# alice never sends him hers without /e2e accept. Then, on alice: a script
# binding /e2e does not switch the module off, a raw line with CR LF and a
# PRIVMSG sent from a rawlog hook do not leave in clear text, and /upgrade
# with PRIVMSGs still in the flood queue encrypts them. A query with E2E on
# whose nick (carol) is no longer reachable is refused, not sent in clear
# text, until /e2e off in that query. Long messages leave through the
# flood queue, paced and in order, a REKEY before the first chunk under
# the new key (the server logs the time of every line: --wire-times);
# chunks still waiting are dropped and reported when rpe2e.pl is loaded,
# on /unload e2e, on disconnect and on /e2e reset, and sent first on
# /upgrade - never in clear text. The "e2e state" and "e2e action"
# signals (a script stands in for a frontend): the state of #test with
# mallory's request, an accept of a request that does not wait (refused),
# a decline (a new state), an accept bound to a fingerprint (another key's: refused; the
# shown one: KEYRSP), "off" for every item told on /unload e2e, and with
# rpe2e.pl loaded (bob) "off" and a refusal - each refusal told with
# its reason ("e2e action refused"), none for what was done. Last, rpe2e.pl
# loads and saves alice's keyring and keeps what only the module writes.
#
# A second fake server plays a second network (chatnet E2E2): alice is on
# both, erin (native module) on E2E2 only. alice's keyring keeps the two
# networks apart ("E2E\x1f#test", "E2E2\x1f#test", each with its own key),
# while rpe2e.pl's (bob's) stays unscoped, and everything on the wire is
# bare. At the very end bob unloads rpe2e.pl and the native module takes
# his old, unscoped keyring over (one network): nothing to do for him.
#
# Each client has its own --home and HOME under a temporary directory and
# runs in a private tmux server (-L e2e-live-<pid>); only that tmux server
# and the fake server started here are stopped at the end.
#
# Needs: tmux, python3, the build with Perl support, and the Perl modules
# of rpe2e.pl (Crypt::NaCl::Sodium, FFI::Platypus - e.g. PERL5LIB pointing
# to a local::lib). Without them the test is skipped (exit 77).
set -u

BUILD=$(cd "${1:?build dir}" && pwd)
SRC=$(cd "${2:?source dir}" && pwd)
SCRIPT=${RPE2E_SCRIPT:-}
IRCD=$SRC/tests/e2e/e2eircd.py

skip() { echo "SKIP: $*"; exit 77; }
command -v tmux >/dev/null 2>&1 || skip "tmux not found"
command -v python3 >/dev/null 2>&1 || skip "python3 not found"
[ -n "$SCRIPT" ] && [ -f "$SCRIPT" ] || skip "set RPE2E_SCRIPT to a copy of rpe2e.pl (from repartee, see the top of this file) to run this test"
[ -f "$IRCD" ] || skip "$IRCD not found"
[ -f "$BUILD/src/perl/libperl_core.so" ] || skip "erssi built without Perl"
[ -f "$BUILD/src/e2e/libe2e_core.so" ] || skip "erssi built without e2e"
perl -MCrypt::NaCl::Sodium -MFFI::Platypus -MFFI::CheckLib -e 1 2>/dev/null ||
    skip "Perl modules of rpe2e.pl missing (Crypt::NaCl::Sodium, FFI::Platypus; set PERL5LIB)"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/e2e-live.XXXXXX")
SOCK="e2e-live-$$"
CHAN="#test"
IRCD_PID=
ok=0
fail=0
pass() { printf '  ok   %s\n' "$*"; ok=$((ok + 1)); }
bad()  { printf '  FAIL %s\n' "$*"; fail=$((fail + 1)); }

cleanup() {
    tmux -L "$SOCK" kill-server 2>/dev/null || true
    if [ -n "$IRCD_PID" ]; then kill $IRCD_PID 2>/dev/null || true; fi
    if [ "${KEEP_WORK:-}" = 1 ]; then echo "work dir kept: $WORK"; else rm -rf "$WORK"; fi
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# this build, installed under $WORK (modules and Perl libraries in the
# installed layout); the binary finds the modules through <home>/modules
meson install -C "$BUILD" --destdir "$WORK/inst" --quiet >"$WORK/install.log" 2>&1 ||
    { cat "$WORK/install.log"; echo "meson install failed"; exit 1; }
ERSSI=$(find "$WORK/inst" -path '*/bin/erssi' -type f | head -n 1)
MODDIR=$(dirname "$(find "$WORK/inst" -name libirc_core.so | head -n 1)")
PERLLIB=$(dirname "$(find "$WORK/inst" -name Irssi.pm | head -n 1)")
[ -x "$ERSSI" ] && [ -d "$MODDIR" ] && [ -d "$PERLLIB" ] || { echo "installed tree incomplete"; exit 1; }

PORT=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
PORT2=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
PORT3=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
# E2E compares names in ASCII ([ and { are two channels), E2E2 by Unicode
python3 "$IRCD" --port "$PORT" --wire "$WORK/wire.log" --wire-times "$WORK/wire-times.log" --casemapping ascii &
IRCD_PID=$!
python3 "$IRCD" --port "$PORT2" --wire "$WORK/wire2.log" --wire-times "$WORK/wire2-times.log" --network E2E2Test --casemapping utf8 &
IRCD_PID="$IRCD_PID $!"
# E2E3 folds as rfc1459 and names no CASEMAPPING: decided after its MOTD
python3 "$IRCD" --port "$PORT3" --wire "$WORK/wire3.log" --network E2E3Test --casemapping rfc1459 --hide-casemapping &
IRCD_PID="$IRCD_PID $!"
for port in "$PORT" "$PORT2" "$PORT3"; do
    i=0
    while ! python3 -c "import socket; socket.create_connection(('127.0.0.1', $port), 1)" 2>/dev/null; do
        i=$((i + 1)); [ $i -gt 50 ] && { echo "test server did not start"; exit 1; }
        sleep 0.1
    done
done

for nick in alice bob mallory carol erin; do
    home="$WORK/$nick"
    mkdir -p "$home/modules" "$home/logs" "$home/scripts/autorun" "$WORK/home-$nick"
    for so in "$MODDIR"/*.so; do ln -s "$so" "$home/modules/"; done
    [ "$nick" = bob ] && ln -s "$SCRIPT" "$home/scripts/autorun/rpe2e.pl"
    # the theme of this source tree, from --home: without it erssi reads
    # the theme from THEMESDIR (the prefix, not the --destdir tree), when
    # one is installed there, and the log lines the long message check
    # reads depend on what this machine has installed
    cp "$SRC/themes/default.theme" "$home/default.theme"
    # alice on both networks, erin on E2E2 only, the others on E2E
    case $nick in
    alice) nets="E2E:$PORT E2E2:$PORT2 E2E3:$PORT3" ;;
    erin)  nets="E2E2:$PORT2" ;;
    *)     nets="E2E:$PORT" ;;
    esac
    srv= net= chn=
    for n in $nets; do
        srv="$srv${srv:+, }{ address = \"127.0.0.1\"; chatnet = \"${n%%:*}\"; port = \"${n#*:}\"; autoconnect = \"yes\"; }"
        net="$net ${n%%:*} = { type = \"IRC\"; };"
        chn="$chn${chn:+, }{ name = \"$CHAN\"; chatnet = \"${n%%:*}\"; autojoin = \"yes\"; }"
    done
    cat > "$home/config" <<EOF
servers = ( $srv );
chatnets = {$net };
channels = ( $chn );
logs = { "$home/all.log" = { auto_open = "yes"; level = "ALL"; }; };
settings = {
  core = { real_name = "e2e test"; user_name = "$nick"; nick = "$nick"; autoload_modules = "irc dcc flood notifylist perl otr fe_web anti_floodnet e2e"; };
  "fe-common/core" = { theme = "default"; autolog = "yes"; autolog_path = "$home/logs/\$tag/\$0.log"; autolog_level = "ALL"; };
  "perl/core" = { perl_use_lib = "$PERLLIB"; };
};
EOF
    # ASan (a sanitizer build): reports go to files, checked at the end
    tmux -L "$SOCK" new-session -d -s "$nick" -x 220 -y 50 \
        "env -u LC_ALL LANG=C.UTF-8 HOME=$WORK/home-$nick PERL5LIB=${PERL5LIB:-} ASAN_OPTIONS=detect_leaks=0:log_path=$WORK/asan-$nick UBSAN_OPTIONS=print_stacktrace=1:log_path=$WORK/ubsan-$nick $ERSSI --home=$home"
done

say() {  # say <nick> <text>: type one line into that erssi
    tmux -L "$SOCK" send-keys -t "$1" -l -- "$2"
    tmux -L "$SOCK" send-keys -t "$1" Enter
    sleep 1
}

quick() {  # quick <nick> <text>: the same, without waiting
    tmux -L "$SOCK" send-keys -t "$1" -l -- "$2"
    tmux -L "$SOCK" send-keys -t "$1" Enter
    sleep 0.2
}

wait_for() {  # wait_for <file> <fixed string> <seconds>
    local n=0
    while [ $n -lt "$3" ]; do
        grep -qF -- "$2" "$1" 2>/dev/null && return 0
        sleep 1
        n=$((n + 1))
    done
    return 1
}

echo "erssi:    $ERSSI"
echo "rpe2e.pl: $SCRIPT"

n=0
until grep -q "bob.*joined $CHAN" "$WORK/alice/logs/E2E/$CHAN.log" 2>/dev/null ||
      grep -q "alice.*joined $CHAN" "$WORK/bob/logs/E2E/$CHAN.log" 2>/dev/null; do
    n=$((n + 1)); [ $n -gt 40 ] && { bad "clients did not join $CHAN"; exit 1; }
    sleep 1
done
pass "alice and bob on $CHAN"

for nick in alice bob; do
    if wait_for "$WORK/$nick/rpe2e/keyring.json" '"identity"' 15; then
        pass "$nick: identity in rpe2e/keyring.json"
    else
        bad "$nick: no identity in the keyring"
    fi
done
# bob runs rpe2e.pl AND has the native module loaded: the module must
# notice the script (whichever loaded first) and leave everything to it
if grep -qF "Error in script" "$WORK/bob/all.log" 2>/dev/null; then
    bad "bob: rpe2e.pl did not load"; sed -n '/Error in script/,+3p' "$WORK/bob/all.log"; exit 1
fi
say bob "/e2e status"
if wait_for "$WORK/bob/all.log" "(the native e2e module is inactive while the rpe2e.pl script is loaded)" 10 &&
   grep -qE "\[E2E\] identity=[0-9a-f]{32} " "$WORK/bob/all.log"; then
    pass "bob: rpe2e.pl answers /e2e, the native module stays inactive"
else
    bad "bob: rpe2e.pl and the native module are not as expected"
fi
# the warning comes while the modules load, before the log is open: it is
# on bob's screen, in the status window
say bob "/window 1"
if grep -qF "the rpe2e.pl script is loaded: the native e2e module leaves encryption to it" "$WORK/bob/all.log" ||
   tmux -L "$SOCK" capture-pane -p -J -t bob -S -1000 | grep -qF "the rpe2e.pl script is loaded: the native e2e module"; then
    pass "bob: the native module warned about rpe2e.pl"
else
    bad "bob: no warning about rpe2e.pl from the native module"
fi
say alice "/e2e status"
if wait_for "$WORK/alice/all.log" "[E2E] identity=" 10 &&
   ! grep -qF "native e2e module is inactive" "$WORK/alice/all.log"; then
    pass "alice: the native module is active"
else
    bad "alice: the native module is not active"
fi

# the statusbar item e2e, in a root bar of its own right above the prompt
# (position 50: below the info bar, above the prompt at 100) - its line is
# the second last of the screen
for nick in alice bob; do
    say "$nick" "/statusbar add -type root -placement bottom -position 50 e2etest"
    say "$nick" "/statusbar additem e2e e2etest"
done
sbar() {  # sbar <nick>: the e2etest bar on that screen
    tmux -L "$SOCK" capture-pane -p -t "$1" | tail -n 2 | head -n 1
}
sbar_shows() {  # sbar_shows <nick> <fixed string> <seconds>
    local n=0
    while [ $n -lt "$3" ]; do
        sbar "$1" | grep -qF -- "$2" && return 0
        sleep 1
        n=$((n + 1))
    done
    return 1
}
sbar_check() {  # sbar_check <nick> <text shown> <ERE not shown, or ""> <what>
    if sbar_shows "$1" "$2" 10 && { [ -z "$3" ] || ! sbar "$1" | grep -qE -- "$3"; }; then
        pass "statusbar: $4"
    else
        bad "statusbar: $4 (the bar: '$(sbar "$1")')"
    fi
}
sbar_hidden() {  # sbar_hidden <nick> <what>: no e2e item on the bar
    sleep 2
    if sbar "$1" | grep -qi -- "e2e"; then
        bad "statusbar: $2 (the bar: '$(sbar "$1")')"
    else
        pass "statusbar: $2"
    fi
}

POLISH="zażółć gęślą jaźń"
LONG_A=$(python3 -c 'print(" ".join("zażółć%d" % i for i in range(1, 41)))')
LONG_B=$(python3 -c 'print(" ".join("gęślą%d" % i for i in range(1, 41)))')

# ---- channel ----
say alice "/join -E2E $CHAN"
say bob "/window goto $CHAN"
say alice "/e2e on"
say bob "/e2e on"
say alice "/e2e handshake bob"
sleep 2
if wait_for "$WORK/bob/logs/E2E/$CHAN.log" "Pending key exchange from alice" 10; then
    pass "bob (rpe2e.pl) got the native KEYREQ"
else
    bad "bob did not get alice's KEYREQ"
fi
say bob "/e2e accept alice"
sleep 2
# bob's key arrived with his answer, but his own request for alice's key
# waits for her decision as well
if wait_for "$WORK/alice/logs/E2E/$CHAN.log" "Pending key exchange from bob" 10; then
    pass "alice (native) got bob's reciprocal KEYREQ and asks before answering"
else
    bad "alice did not ask about bob's reciprocal KEYREQ"
fi
say alice "/e2e accept bob"
sleep 2

say alice "secret message from alice"
say bob "reply from bob"
say alice "$POLISH"
say bob "$POLISH from bob"
say alice "$LONG_A"
say bob "$LONG_B"
say alice "/me waves"
say bob "/me nods"
say alice "...an ellipsis line"
say alice "/quote privmsg $CHAN :quoted in lower case"
say alice "/msg $CHAN sent with msg"
# a % in printed text is shown as it is
say alice "/e2e verify 100%done"
sleep 3

who() {  # who <log under $WORK>: the client it belongs to
    local rel=${1#"$WORK"/}
    echo "${rel%%/*}"
}
check() {  # check <receiver log> <text>
    # each encrypted line holds the flood queue 4 s and more (irssi's
    # delay after a long line): what was typed last can take a while
    if wait_for "$1" "$2" 30; then pass "$(who "$1") sees: $2"
    else bad "$(who "$1") does not see: $2"; fi
}
check "$WORK/bob/logs/E2E/$CHAN.log" "secret message from alice"
check "$WORK/alice/logs/E2E/$CHAN.log" "reply from bob"
check "$WORK/bob/logs/E2E/$CHAN.log" "$POLISH"
check "$WORK/alice/logs/E2E/$CHAN.log" "$POLISH from bob"
check "$WORK/bob/logs/E2E/$CHAN.log" "alice waves"
check "$WORK/alice/logs/E2E/$CHAN.log" "bob nods"
check "$WORK/bob/logs/E2E/$CHAN.log" "an ellipsis line"
check "$WORK/bob/logs/E2E/$CHAN.log" "quoted in lower case"
check "$WORK/bob/logs/E2E/$CHAN.log" "sent with msg"
check "$WORK/alice/logs/E2E/$CHAN.log" "cannot resolve handle for 100%done"
sbar_check alice "E2E" "waiting|blocked|error" "alice in E2E's #test after the exchange: E2E"
# the shipped config (the test's config has no statusbar of its own) has
# no e2e item: added as /help e2e says, it shows in the info bar (the
# line above e2etest) after the window item
say alice "/statusbar additem -after window e2e info"
if tmux -L "$SOCK" capture-pane -p -t alice | tail -n 3 | head -n 1 | grep -qE "$CHAN.*E2E"; then
    pass "statusbar: the e2e item added to the shipped info bar, after the window item"
else
    bad "statusbar: the e2e item added to the shipped info bar is not shown (the bar: '$(tmux -L "$SOCK" capture-pane -p -t alice | tail -n 3 | head -n 1)')"
fi
# bob has rpe2e.pl loaded: the native module shows nothing (the script's
# state is not the module's to show)
sbar_hidden bob "bob's #test while rpe2e.pl is loaded: no e2e item"
say alice "/window 1"
sbar_hidden alice "alice's status window: no e2e item"
# back on E2E (the status window's server may be another network)
say alice "/join -E2E $CHAN"

# ---- query ----
say alice "/query bob"
say bob "/query alice"
say alice "/e2e on"
say bob "/e2e on"
say alice "/e2e handshake bob"
sleep 2
if wait_for "$WORK/bob/logs/E2E/alice.log" "Pending key exchange from alice" 10; then
    pass "bob (rpe2e.pl) got the native DM KEYREQ"
else
    bad "bob did not get alice's DM KEYREQ"
fi
say bob "/e2e accept alice"
sleep 2
if wait_for "$WORK/alice/logs/E2E/bob.log" "Pending key exchange from bob" 10; then
    pass "alice (native) got bob's reciprocal DM KEYREQ"
else
    bad "alice did not get bob's reciprocal DM KEYREQ"
fi
# the only request waiting in the query: no argument needed
say alice "/e2e accept"
sleep 2
say alice "private from alice $POLISH"
say bob "private from bob $POLISH"
say alice "$LONG_B"
say bob "$LONG_A"
sleep 3
check "$WORK/bob/logs/E2E/alice.log" "private from alice $POLISH"
check "$WORK/alice/logs/E2E/bob.log" "private from bob $POLISH"
sbar_check alice "E2E" "waiting|blocked|error" "alice in bob's query after the exchange: E2E"

# the long messages: several encrypted chunks, each shown as a line; put
# together they are the message
long_check() {  # long_check <receiver log> <sender nick> <text> <what> [<seconds>]
    # each line of a long message leaves after the flood delay of the
    # one before (2 s + 1 s per 100 bytes)
    local n=0
    until long_check_once "$1" "$2" "$3"; do
        n=$((n + 1)); [ $n -ge "${5:-20}" ] && { bad "$4"; return; }
        sleep 1
    done
    pass "$4"
}
long_check_once() {
    python3 - "$1" "$2" "$3" <<'EOF'
import re, sys
log, nick, text = sys.argv[1], sys.argv[2], sys.argv[3]
bodies = []
with open(log, encoding="utf-8", errors="strict") as fh:
    for line in fh:
        # "<alice> text" (default theme) or "alice❯ text" (erssi's)
        m = re.match(r"^\S+\s+(?:<[ @+%~&]?" + re.escape(nick) + r">|[@+%~&]?" +
                     re.escape(nick) + "\u276f) (.*)$", line.rstrip("\n"))
        if m:
            bodies.append(m.group(1))
joined = "".join(bodies).replace(" ", "")
sys.exit(0 if text.replace(" ", "") in joined else 1)
EOF
}
long_check "$WORK/bob/logs/E2E/$CHAN.log" alice "$LONG_A" "channel: bob reads alice's long message (chunked)"
long_check "$WORK/alice/logs/E2E/$CHAN.log" bob "$LONG_B" "channel: alice reads bob's long message (chunked)"
long_check "$WORK/bob/logs/E2E/alice.log" alice "$LONG_B" "query: bob reads alice's long message (chunked)"
long_check "$WORK/alice/logs/E2E/bob.log" bob "$LONG_A" "query: alice reads bob's long message (chunked)"

# ---- what crossed the server ----
python3 - "$WORK/wire.log" "$CHAN" <<'EOF' && pass "server: every message is ciphertext, chunked ones too" || bad "server: plain text or missing chunks"
import sys
log, chan = sys.argv[1], sys.argv[2]
priv = [l.rstrip("\n") for l in open(log, encoding="utf-8") if " PRIVMSG " in l]
targets = {"alice": chan, "bob": chan}
total = cipher = 0
multi = {}
for l in priv:
    sender, _, target, text = l.split(" ", 3)
    if target not in (chan, "alice", "bob"):
        continue
    total += 1
    if text.startswith(":+RPE2E01 "):
        cipher += 1
        msgid, part = text.split()[1], text.split()[3]
        if not part.startswith("1/1"):
            multi.setdefault((sender, target), set()).add(msgid)
print(f"  ({cipher}/{total} PRIVMSGs encrypted; chunked messages: {sum(len(v) for v in multi.values())})")
ok = total >= 20 and cipher == total
# a long message from each side, on the channel and in the query
ok = ok and all(len(multi.get(k, ())) >= 1 for k in (("alice", chan), ("bob", chan), ("alice", "bob"), ("bob", "alice")))
sys.exit(0 if ok else 1)
EOF
# the words are looked for in what is neither a +RPE2E01 wire nor an
# RPEE2E key exchange: a case-insensitive word can turn up in base64
# ("nods" did, in about 0.7% of runs)
wire_plain() {  # wire_plain <wire log>: the text of every other line
    python3 - "$1" <<'PY'
import re, sys
WIRE = re.compile(r'^\+RPE2E01 [0-9a-f]+ \d+ \d+/\d+ [A-Za-z0-9+/=]+:[A-Za-z0-9+/=]+$')
KX = re.compile(r'^\x01?RPEE2E [A-Z]+( (v|c|p|e|wn|w|n|s)=\S*)*\x01?$')
for l in open(sys.argv[1], encoding="utf-8"):
    parts = l.rstrip("\n").split(" ", 3)
    text = parts[3][1:] if len(parts) == 4 and parts[3].startswith(":") else l
    if not (WIRE.match(text) or KX.match(text)):
        print(text)
PY
}
wire_plain "$WORK/wire.log" > "$WORK/wire-plain.log"
leak=0
for word in "secret message" "reply from bob" "waves" "nods" "ellipsis" "lower case" "sent with msg" \
            "private from" "zaż" "gęś" "jaźń" "zażółć1" "gęślą1"; do
    if grep -qiF -- "$word" "$WORK/wire-plain.log"; then bad "plain text crossed the server: $word"; leak=1; fi
done
[ $leak = 0 ] && pass "no plain text crossed the server"
if grep -q "RPEE2E KEYREQ" "$WORK/wire.log" && grep -q "RPEE2E KEYRSP" "$WORK/wire.log"; then
    pass "key exchange via CTCP KEYREQ/KEYRSP"
else
    bad "no KEYREQ/KEYRSP on the server"
fi

# ---- the keyrings: the same format on both sides ----
# alice's (native) entries are kept per network ("E2E\x1f..."), bob's
# (rpe2e.pl) are not; the wire between them names the bare contexts
python3 - "$WORK/alice/rpe2e/keyring.json" "$WORK/bob/rpe2e/keyring.json" <<'EOF' && pass "keyrings: native and rpe2e.pl files hold each other's identity" || bad "keyrings do not match"
import json, sys, stat, os
alice, bob = (json.load(open(p, encoding="utf-8")) for p in sys.argv[1:3])
for p in sys.argv[1:3]:
    assert stat.S_IMODE(os.stat(p).st_mode) == 0o600, p
fa, fb = alice["identity"]["fp"], bob["identity"]["fp"]
assert fb in alice["peers"] and alice["peers"][fb]["status"] == "trusted", "alice lacks bob"
assert fa in bob["peers"] and bob["peers"][fa]["status"] == "trusted", "bob lacks alice"
assert alice["incoming"]["bob@127.0.0.1|E2E\x1f#test"]["status"] == "trusted"
assert bob["incoming"]["alice@127.0.0.1|#test"]["status"] == "trusted"
assert alice["incoming"]["bob@127.0.0.1|E2E\x1f@alice@127.0.0.1"]["status"] == "trusted"
assert bob["incoming"]["alice@127.0.0.1|@bob@127.0.0.1"]["status"] == "trusted"
assert alice["channels"]["E2E\x1f#test"]["enabled"] and alice["channels"]["E2E\x1f@bob@127.0.0.1"]["enabled"]
# nothing unscoped from the module, nothing scoped from rpe2e.pl
for store in ("channels", "outgoing", "incoming", "accepted", "pending", "outgoing_recipients", "pending_inbound"):
    for k in alice.get(store, {}):
        assert "\x1f" in k, (store, k)
assert not any("\x1f" in k for k in bob["channels"]), "rpe2e.pl wrote a scoped entry"
EOF
if grep -aq $'\x1f' "$WORK/wire.log"; then
    bad "a scoped context crossed the server"
else
    pass "server: only bare contexts on the wire"
fi

# ---- mallory: an outsider gets no key without /e2e accept ----
say alice "/join -E2E $CHAN"
say mallory "/window goto $CHAN"
say mallory "/e2e mode auto"
say mallory "hello from mallory"
# handshake NOTICEs go through the flood queue: mallory answers bob too
if wait_for "$WORK/alice/logs/E2E/$CHAN.log" "Pending key exchange from mallory" 30; then
    pass "alice: mallory's request for her key waits for /e2e accept"
else
    bad "alice: no pending request from mallory"
fi
MALLORY_AT=$(date +%s)
sbar_check alice "1 to accept" "" "alice's #test with mallory's request waiting: 1 to accept"
# the prompt names the address and the network:channel, and how to answer
# (alice holds mallory's key already: she asked him for it)
if grep -F "Pending key exchange from mallory (mallory@127.0.0.1) for E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log" |
   grep -qF "Run /e2e accept mallory@127.0.0.1 or /e2e decline mallory@127.0.0.1 (/e2e pending lists all)"; then
    pass "alice: the prompt names mallory's address, the network and the commands"
else
    bad "alice: the pending prompt is not as expected"
    grep -aF "Pending key exchange from mallory" "$WORK/alice/logs/E2E/$CHAN.log" | sed 's/^/    /'
fi
say alice "/e2e pending"
if wait_for "$WORK/alice/all.log" "mallory (mallory@127.0.0.1) on E2E:$CHAN" 5; then
    pass "/e2e pending lists mallory's request"
else
    bad "/e2e pending does not list mallory's request"
fi
say alice "/e2e list"
if wait_for "$WORK/alice/logs/E2E/$CHAN.log" "pending: mallory (mallory@127.0.0.1)" 5; then
    pass "/e2e list in #test shows mallory's request"
else
    bad "/e2e list in #test does not show mallory's request"
fi
if grep -aq "^alice NOTICE mallory :.RPEE2E KEYRSP" "$WORK/wire.log"; then
    bad "alice sent her key (KEYRSP) to mallory without /e2e accept"
else
    pass "alice sent mallory no KEYRSP"
fi
say alice "members only after mallory came"
sleep 3
check "$WORK/bob/logs/E2E/$CHAN.log" "members only after mallory came"
if grep -qF "members only after mallory came" "$WORK/mallory/logs/E2E/$CHAN.log" 2>/dev/null; then
    bad "mallory reads alice's channel messages"
else
    pass "mallory cannot read alice's channel messages"
fi

# ---- a script that binds /e2e is not rpe2e.pl ----
cat > "$WORK/alice/scripts/e2ealias.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'e2ealias');
Irssi::command_bind('e2e', sub { });
PERL
say alice "/script load e2ealias"
say alice "after another script bound e2e"
sleep 2
check "$WORK/bob/logs/E2E/$CHAN.log" "after another script bound e2e"
say alice "/script unload e2ealias"

# ---- a PRIVMSG from a rawlog hook while ciphertext goes out ----
cat > "$WORK/alice/scripts/e2erawlog.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'e2erawlog');
my $done = 0;
Irssi::signal_add('rawlog', sub {
    my ($rawlog, $data) = @_;
    return if $done || $data !~ /^<< PRIVMSG #test :\+RPE2E01 /;
    $done = 1;
    my $server = Irssi::server_find_tag('E2E');
    $server->send_raw_now('PRIVMSG #test :sent from a rawlog hook');
});
PERL
say alice "/script load e2erawlog"
say alice "this line triggers the hook"
sleep 2
check "$WORK/bob/logs/E2E/$CHAN.log" "sent from a rawlog hook"
say alice "/script unload e2erawlog"

# ---- a raw line with CR LF in it ----
say alice "/script exec Irssi::active_server->send_raw('PRIVMSG #pub :hello'.chr(13).chr(10).'PRIVMSG $CHAN :raw line with CRLF')"
sleep 1
say alice "/script exec Irssi::active_server->send_raw('PRIVMSG #pub :hello'.chr(13).'PRIVMSG $CHAN :raw line with CR')"
sleep 2

python3 - "$WORK/wire.log" <<'PY' && pass "server: the checks above sent no plain text" || bad "server: plain text from the checks above"
import sys
bad = 0
for l in open(sys.argv[1], encoding="utf-8", errors="replace"):
    for word in ("hello from mallory", "members only", "another script bound", "triggers the hook",
                 "rawlog hook", "raw line with"):
        if word in l:
            print("  plain text:", l.rstrip())
            bad += 1
sys.exit(1 if bad else 0)
PY

# ---- E2E on in a query, the nick's address no longer known ----
say alice "/query carol"
say alice "/e2e on"
say alice "/window close"
say carol "/part $CHAN"
sleep 1
say alice "/join -E2E $CHAN"
say alice "/msg carol unresolved dm secret"
sleep 2
if grep -aqF "unresolved dm secret" "$WORK/wire.log"; then
    bad "a DM to carol (E2E on, address unknown) went out in plain text"
else
    pass "a DM to carol (E2E on, address unknown) is refused"
fi
# turned off in carol's query: clear text is the user's choice now
say alice "/query carol"
say alice "/e2e off"
say alice "/msg carol clear after off"
sleep 2
if grep -aqF "alice PRIVMSG carol :clear after off" "$WORK/wire.log"; then
    pass "/e2e off in the query turns E2E off for carol's last address"
else
    bad "/e2e off in carol's query did not turn E2E off"
fi
say alice "/window close"

# ---- a second network (E2E2): its own keys, its own settings ----
say erin "/window goto $CHAN"
n=0
until grep -qF "erin" "$WORK/alice/logs/E2E2/$CHAN.log" 2>/dev/null ||
      grep -qF "alice" "$WORK/erin/logs/E2E2/$CHAN.log" 2>/dev/null; do
    n=$((n + 1)); [ $n -gt 20 ] && break
    sleep 1
done
# E2E is on for #test on E2E only: the other network's #test is another
# channel and gets clear text
say alice "/join -E2E2 $CHAN"
sbar_check alice "e2e off" "" "alice in E2E2's #test before /e2e on: e2e off"
say alice "net2 plain hello"
sleep 1
if grep -aqF "alice PRIVMSG $CHAN :net2 plain hello" "$WORK/wire2.log"; then
    pass "E2E2: #test there is not encrypted (E2E is on for E2E's #test only)"
else
    bad "E2E2: the clear-text message is not on the second network"
fi
check "$WORK/erin/logs/E2E2/$CHAN.log" "net2 plain hello"
if grep -aqF "net2 plain hello" "$WORK/wire.log"; then
    bad "E2E2's message crossed the first network"
else
    pass "E2E2's message stayed on its network"
fi
# turned on there: a key of its own
say alice "/e2e on"
sbar_check alice "E2E waiting for keys" "" "alice in E2E2's #test after /e2e on, no keys: waiting"
# another process (rpe2e.pl) changes the keyring: seen at the next switch
python3 - "$WORK/alice/rpe2e/keyring.json" 0 <<'PY' || bad "the keyring edit for the statusbar check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["channels"]["E2E2\x1f#test"]["enabled"] = int(sys.argv[2])
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say alice "/window 1"
say alice "/join -E2E2 $CHAN"
sbar_check alice "e2e off" "" "a keyring written by another process is shown after a window switch"
python3 - "$WORK/alice/rpe2e/keyring.json" 1 <<'PY' || bad "the keyring edit for the statusbar check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["channels"]["E2E2\x1f#test"]["enabled"] = int(sys.argv[2])
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say alice "/window 1"
say alice "/join -E2E2 $CHAN"
sbar_check alice "E2E waiting for keys" "" "and changed back"
say alice "net2 secret hello"
sleep 1
python3 - "$WORK/alice/rpe2e/keyring.json" <<'EOF' && pass "alice: E2E and E2E2 #test have their own keys" || bad "alice: the networks share a key or a config"
import json, sys
k = json.load(open(sys.argv[1], encoding="utf-8"))
a, b = k["outgoing"]["E2E\x1f#test"], k["outgoing"]["E2E2\x1f#test"]
assert a["sk"] != b["sk"], "same key"
assert k["channels"]["E2E2\x1f#test"]["enabled"] and k["channels"]["E2E\x1f#test"]["enabled"]
assert "#test" not in k["channels"] and "#test" not in k["outgoing"]
EOF
if grep -aqF "net2 secret hello" "$WORK/wire2.log" "$WORK/wire.log" ||
   ! grep -aq "^alice PRIVMSG $CHAN :+RPE2E01 " "$WORK/wire2.log"; then
    bad "E2E2: the message after /e2e on is not ciphertext"
else
    pass "E2E2: encrypted after /e2e on there"
fi
if grep -aqF "net2 secret hello" "$WORK/erin/logs/E2E2/$CHAN.log" 2>/dev/null; then
    bad "erin reads alice without a key"
else
    pass "erin (no key) cannot read alice on E2E2"
fi
say alice "/join -E2E $CHAN"
say alice "back on the first network"
check "$WORK/bob/logs/E2E/$CHAN.log" "back on the first network"

# ---- CASEMAPPING: names compared as each server compares them ----
# E2E2 (utf8): #Żaba is #żaba to the server, never clear text there
say alice "/join -E2E2 #żaba"
say alice "/e2e on"
say alice "/msg -E2E2 #Żaba unicode case secret"
say alice "/msg -E2E2 #żaba unicode exact secret"
sleep 1
if grep -aqF "unicode case secret" "$WORK/wire2.log" "$WORK/wire.log"; then
    bad "utf8 CASEMAPPING: #Żaba (E2E on for #żaba) went out in plain text"
else
    pass "utf8 CASEMAPPING: #Żaba (E2E on for #żaba) is not sent in plain text"
fi
if wait_for "$WORK/alice/all.log" "the same channel as #Żaba for the server" 5; then
    pass "alice is told #Żaba is the same channel as #żaba"
else
    bad "alice is not told why #Żaba was not sent"
fi
if grep -aq "^alice PRIVMSG #żaba :+RPE2E01 " "$WORK/wire2.log" &&
   ! grep -aqF "unicode exact secret" "$WORK/wire2.log"; then
    pass "utf8 CASEMAPPING: #żaba itself is encrypted"
else
    bad "utf8 CASEMAPPING: #żaba itself is not encrypted"
fi
# a name that is not UTF-8 cannot be folded as a utf8 server does
say alice "/script exec Irssi::server_find_tag('E2E2')->command('msg #a'.chr(255).' not utf8 secret')"
sleep 1
if grep -aqF "not utf8 secret" "$WORK/wire2.log" "$WORK/wire.log"; then
    bad "utf8 CASEMAPPING: a channel name that is not UTF-8 went out in plain text"
else
    pass "utf8 CASEMAPPING: a channel name that is not UTF-8 is not sent in plain text"
fi
if wait_for "$WORK/alice/all.log" " may be the same channel as " 5; then
    pass "alice is told why the name that is not UTF-8 was not sent"
else
    bad "alice is not told why the name that is not UTF-8 was not sent"
fi
# E2E (ascii): #a{b is another channel than #a[b, #A[B is the same
say alice "/join -E2E #a[b"
say alice "/e2e on"
say alice "/msg -E2E #a{b bracket plain ok"
say alice "/msg -E2E #A[B bracket upper secret"
sleep 1
if grep -aqF "alice PRIVMSG #a{b :bracket plain ok" "$WORK/wire.log"; then
    pass "ascii CASEMAPPING: #a{b (another channel than #a[b) gets clear text"
else
    bad "ascii CASEMAPPING: #a{b was refused, though the server keeps it apart from #a[b"
fi
if grep -aqF "bracket upper secret" "$WORK/wire.log" "$WORK/wire2.log"; then
    bad "ascii CASEMAPPING: #A[B (E2E on for #a[b) went out in plain text"
else
    pass "ascii CASEMAPPING: #A[B (E2E on for #a[b) is not sent in plain text"
fi
# E2E3 names no CASEMAPPING: after its MOTD that is rfc1459, where #Żaba
# is another channel than #żaba (before it, the widest fold)
say alice "/join -E2E3 #żaba"
say alice "/e2e on"
say alice "/msg -E2E3 #Żaba rfc1459 plain before upgrade"
if wait_for "$WORK/wire3.log" "alice PRIVMSG #Żaba :rfc1459 plain before upgrade" 10; then
    pass "no CASEMAPPING, MOTD over: rfc1459 (#Żaba is another channel than #żaba)"
else
    bad "no CASEMAPPING, MOTD over: #Żaba was refused as if the fold were Unicode"
fi
say alice "/join -E2E $CHAN"

# ---- mallory asks again: told once in 5 minutes ----
# mallory's own request to alice expires after 120 s; then he writes
# again and asks again: the request waits, the prompt is not repeated
now=$(date +%s)
[ $((MALLORY_AT + 125 - now)) -gt 0 ] && sleep $((MALLORY_AT + 125 - now))
say alice "/join -E2E $CHAN"
say mallory "/window goto $CHAN"
say mallory "hello again from mallory"
say mallory "/e2e handshake alice"
n=0
while [ "$(grep -ac "^mallory NOTICE alice :.RPEE2E KEYREQ " "$WORK/wire.log")" -lt 2 ] && [ $n -lt 15 ]; do
    sleep 1; n=$((n + 1))
done
sleep 3
if [ "$(grep -ac "^mallory NOTICE alice :.RPEE2E KEYREQ " "$WORK/wire.log")" -lt 2 ]; then
    bad "mallory did not ask alice again (the check proves nothing)"
elif [ "$(grep -c "Pending key exchange from mallory" "$WORK/alice/logs/E2E/$CHAN.log")" -eq 1 ]; then
    pass "mallory asks again: alice is told once"
else
    bad "mallory asks again: alice is told more than once"
    grep -aF "Pending key exchange from mallory" "$WORK/alice/logs/E2E/$CHAN.log" | sed 's/^/    /'
fi

# ---- an old keyring on two networks: refused until /e2e adopt ----
# alice's E2E entries become old ones of no network (as rpe2e.pl or erssi
# before the scope wrote them); E2E2's stay. alice uses two networks, so
# erssi cannot know where they belong: never clear text, never another
# network's rows, until /e2e adopt in the window
python3 - "$WORK/alice/rpe2e/keyring.json" "$WORK/alice-scoped.json" <<'PY' || bad "the legacy rewrite of alice's keyring failed"
import json, re, sys, shutil
path, keep = sys.argv[1], sys.argv[2]
shutil.copyfile(path, keep)
k = json.load(open(path, encoding="utf-8"))
bare = lambda t: re.sub(r"(^|\|)E2E\x1f", r"\1", t)
def walk(v):
    if isinstance(v, dict):
        return {bare(key): (bare(x) if key == "channel" and isinstance(x, str) else walk(x)) for key, x in v.items()}
    if isinstance(v, list):
        return [walk(x) for x in v]
    return v
k = walk(k)
assert "#test" in k["channels"] and "E2E2\x1f#test" in k["channels"]
with open(path, "w", encoding="utf-8") as fh:
    json.dump(k, fh)
PY
say alice "/join -E2E $CHAN"
say alice "after legacy rewrite"
sleep 1
if grep -aqF "after legacy rewrite" "$WORK/wire.log" "$WORK/wire2.log"; then
    bad "an old entry on two networks went out in plain text"
else
    pass "an old entry on two networks is not sent in plain text"
fi
if wait_for "$WORK/alice/logs/E2E/$CHAN.log" "/e2e adopt in this window binds it to E2E" 5; then
    pass "alice is told about /e2e adopt"
else
    bad "alice is not told about /e2e adopt"
fi
say alice "/e2e adopt"
if wait_for "$WORK/alice/logs/E2E/$CHAN.log" "adopted $CHAN for E2E" 5; then
    pass "/e2e adopt binds the old #test entries to E2E"
else
    bad "/e2e adopt did not adopt #test"
fi
say alice "after adopt"
check "$WORK/bob/logs/E2E/$CHAN.log" "after adopt"
# in the query: bob's config and his sessions to alice ("@<own>")
say alice "/query bob"
say alice "/e2e adopt"
if wait_for "$WORK/alice/logs/E2E/bob.log" "adopted @bob@127.0.0.1 for E2E" 5; then
    pass "/e2e adopt in the query binds bob's old entries to E2E"
else
    bad "/e2e adopt did not adopt the query"
fi
say alice "dm after adopt"
say bob "/query alice"
say bob "dm back after adopt"
check "$WORK/bob/logs/E2E/alice.log" "dm after adopt"
check "$WORK/alice/logs/E2E/bob.log" "dm back after adopt"
say alice "/window close"
# a target list with a query E2E is on for: one line cannot be
# encrypted for several - irssi sends such a list in one line
# when the server's TARGMAX allows it
say alice "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG bob,carol :native list dm secret')"
sleep 1
if grep -aqF "native list dm secret" "$WORK/wire.log"; then
    bad "a target list with a query E2E is on for went out in plain text"
else
    pass "a target list with a query E2E is on for is not sent"
fi
# an old bare-nick row (rpe2e.pl's config of a query by nick) with E2E
# on, the nick's address unknown now: every spelling the server takes
# for that nick is refused, not only the stored one (E2E: ascii)
python3 - "$WORK/alice/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the bare-nick check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["channels"]["zed"] = {"enabled": 1, "mode": "normal"}
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say alice "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG ZED :bare nick other case secret')"
say alice "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG zed :bare nick exact secret')"
sleep 1
if grep -aqF "bare nick other case secret" "$WORK/wire.log" ||
   grep -aqF "bare nick exact secret" "$WORK/wire.log"; then
    bad "ZED (an old bare-nick row zed with E2E on) went out in plain text"
else
    pass "ZED (an old bare-nick row zed with E2E on) is not sent in plain text"
fi
python3 - "$WORK/alice/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the bare-nick check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
del k["channels"]["zed"]
json.dump(k, open(p, "w", encoding="utf-8"))
PY
python3 - "$WORK/alice-scoped.json" "$WORK/alice/rpe2e/keyring.json" "$WORK/wire.log" "$WORK/wire2.log" <<'PY' && pass "adopted: E2E's keys as before, E2E2's entries untouched, nothing in clear" || bad "the adoption changed keys or another network's entries"
import json, sys
old, new = (json.load(open(p, encoding="utf-8")) for p in sys.argv[1:3])
s, s2 = "E2E\x1f#test", "E2E2\x1f#test"
assert new["outgoing"][s]["sk"] == old["outgoing"][s]["sk"]
assert new["channels"][s]["adopted"] == "E2E"
assert new["incoming"]["bob@127.0.0.1|" + s]["sk"] == old["incoming"]["bob@127.0.0.1|" + s]["sk"]
assert new["incoming"]["bob@127.0.0.1|E2E\x1f@alice@127.0.0.1"]["sk"] == old["incoming"]["bob@127.0.0.1|E2E\x1f@alice@127.0.0.1"]["sk"]
for store in ("channels", "outgoing"):
    assert new[store][s2] == old[store][s2], store
assert "#test" not in new["channels"] and "#test" not in new["outgoing"]
for w in sys.argv[3:5]:
    for l in open(w, encoding="utf-8", errors="replace"):
        assert "after adopt" not in l and "legacy rewrite" not in l, l
PY
# what the user reads: "<network>:<name>", never the raw separator
say alice "/join -E2E $CHAN"
say alice "/e2e status"
say alice "/e2e list -all"
say alice "/e2e autotrust add $CHAN bob@127.0.0.1"
say alice "/e2e autotrust list"
say alice "/e2e autotrust remove bob@127.0.0.1"
if wait_for "$WORK/alice/all.log" "autotrust add E2E:$CHAN bob@127.0.0.1" 5 &&
   grep -qF "encrypted: " "$WORK/alice/all.log" &&
   grep -F "encrypted: " "$WORK/alice/all.log" | grep -qF "E2E2:$CHAN" &&
   grep -qE "bob@127.0.0.1  E2E:$CHAN  \[trusted\]" "$WORK/alice/all.log" &&
   ! grep -aqE "E2E2?$CHAN" "$WORK/alice/all.log" && ! grep -aq $'\x1f' "$WORK/alice/all.log"; then
    pass "status, list -all and autotrust show <network>:<name>"
else
    bad "status, list -all or autotrust do not show <network>:<name>"
    grep -aE "encrypted:|autotrust|E2E:#|E2E#" "$WORK/alice/all.log" | tail -n 8 | sed 's/^/    /'
fi
# control bytes stored in the keyring (a handle, a nick) are shown as '?',
# never as formatting codes
python3 - "$WORK/alice/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the display check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["peers"]["e" * 32] = {"pk": "", "last_handle": "evil\x03" "4@h", "last_nick": "evil\x02nick",
                        "status": "pending", "first_seen": 1, "last_seen": 1}
k["incoming"]["evil\x03" "4@h|E2E\x1f#test"] = {"fp": "e" * 32, "sk": "", "status": "pending", "created_at": 1}
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say alice "/e2e list -all"
say alice "/e2e list"
python3 - "$WORK/alice/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the display check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
del k["peers"]["e" * 32]
del k["incoming"]["evil\x03" "4@h|E2E\x1f#test"]
json.dump(k, open(p, "w", encoding="utf-8"))
PY
if grep -qF "evil?4@h  [pending]  nick=evil?nick" "$WORK/alice/all.log" &&
   grep -qF "evil?4@h  E2E:$CHAN  [pending]" "$WORK/alice/all.log" &&
   grep -qF "evil?4@h on E2E:$CHAN" "$WORK/alice/all.log"; then
    pass "list and list -all show control bytes of the keyring as '?'"
else
    bad "list or list -all print control bytes of the keyring"
    grep -aF "evil" "$WORK/alice/all.log" | sed 's/^/    /'
fi

# ---- long messages through the flood queue, in order ----
# every encrypted line is longer than 100 bytes, and irssi waits 2 s and
# 1 s per 100 bytes after such a line (wait_cmd): the chunks of a long
# message leave one at a time, at the pace of irssi's flood queue, and a
# message typed after it waits behind them. (The /set values apply to
# the next connection - here the one after /upgrade.)
first_wire() {  # first_wire <mark> <seconds> [<wire-times file>]: the msgid of alice's first wire to $CHAN after the mark
    python3 - "${3:-$WORK/wire-times.log}" "$1" "$2" "$CHAN" <<'PY'
import sys, time
path, mark, limit, chan = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
end = time.time() + limit
while time.time() < end:
    with open(path, encoding="utf-8", errors="replace") as fh:
        for i, l in enumerate(fh):
            f = l.rstrip("\n").split(" ", 4)
            if i >= mark and len(f) == 5 and f[1:4] == ["alice", "PRIVMSG", chan] and f[4].startswith(":+RPE2E01 "):
                print(f[4][1:].split(" ")[1])
                sys.exit(0)
    time.sleep(0.1)
sys.exit(1)
PY
}
wire_groups() {  # wire_groups <mark> [<wire-times file>]: alice's wires to $CHAN after the mark, a line per message in order of its first line: "<msgid> <parts seen, in order> <first line index> <last line index>"
    python3 - "${2:-$WORK/wire-times.log}" "$1" "$CHAN" <<'PY'
import sys
path, mark, chan = sys.argv[1], int(sys.argv[2]), sys.argv[3]
groups = {}
for i, l in enumerate(open(path, encoding="utf-8", errors="replace")):
    f = l.rstrip("\n").split(" ", 4)
    if i >= mark and len(f) == 5 and f[1:4] == ["alice", "PRIVMSG", chan] and f[4].startswith(":+RPE2E01 "):
        w = f[4][1:].split(" ")
        g = groups.setdefault(w[1], [[], i, i])
        g[0].append(w[3])
        g[2] = i
for mid, (parts, first, last) in groups.items():
    print(mid, ",".join(parts), first, last)
PY
}
LONG_C=$(python3 -c 'print(" ".join("pacedlong%d" % i for i in range(1, 61)))')
LONG_C2=$(python3 -c 'print(" ".join("secondlong%d" % i for i in range(1, 25)))')
LONG_D=$(python3 -c 'print(" ".join("rotatedlong%d" % i for i in range(1, 61)))')
LONG_G=$(python3 -c 'print(" ".join("heldlong%d" % i for i in range(1, 61)))')
say alice "/join -E2E $CHAN"
say alice "/set cmds_max_at_once 2"; say alice "/set cmd_queue_speed 1000msec"
# a PRIVMSG sent from a rawlog hook right after the first chunk of a line
# the pump sent (the second half of LONG_C) is not the pump's line: it
# waits behind the others, not between two chunks
cat > "$WORK/alice/scripts/e2epumphook.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'e2epumphook');
my $done = 0;
Irssi::signal_add('rawlog', sub {
    my ($rawlog, $data) = @_;
    return if $done || $data !~ m{^<< PRIVMSG #test :\+RPE2E01 \S+ \d+ 1/2 };
    $done = 1;
    Irssi::server_find_tag('E2E')->send_raw_now('PRIVMSG #test :nested from a rawlog hook');
});
PERL
say alice "/script load e2epumphook"
sleep 5
TIMES_MARK=$(wc -l < "$WORK/wire-times.log")
quick alice "$LONG_C"; quick alice "$LONG_C2"; quick alice "short after long"
if wait_for "$WORK/bob/logs/E2E/$CHAN.log" "short after long" 120; then
    pass "bob sees: short after long"
else
    bad "bob does not see: short after long"
fi
# irssi splits a typed line at the server's line limit: LONG_C is two
# PRIVMSGs, each encrypted to its own chunks (3 and 2), LONG_C2 one (2
# chunks). In order: the chunks of each together, in order, and the
# short message last - each PRIVMSG that waited behind chunks is
# decided when it leaves the module's queue, its own chunks go ahead of
# what waits behind it. Paced: after the first two lines, one at a time
python3 - "$WORK/wire-times.log" "$TIMES_MARK" "$CHAN" <<'PY' && pass "flood queue: the chunks leave in order, paced, before the message typed after them" || bad "flood queue: chunks out of order, or not paced"
import sys
path, mark, chan = sys.argv[1], int(sys.argv[2]), sys.argv[3]
rows = []
for i, l in enumerate(open(path, encoding="utf-8", errors="replace")):
    f = l.rstrip("\n").split(" ", 4)
    if i >= mark and len(f) == 5 and f[1:4] == ["alice", "PRIVMSG", chan] and f[4].startswith(":+RPE2E01 "):
        w = f[4][1:].split(" ")
        rows.append((float(f[0]), w[1], w[3]))
seq = [mid for _, mid, _ in rows]
order = []
for mid in seq:
    if mid not in order:
        order.append(mid)
# up to the short message (1/1), the first single-line message
short = next((k for k, mid in enumerate(order) if [p for _, m, p in rows if m == mid] == ["1/1"]), None)
if short is None or short < 2:
    print("  (no short message after the long ones: %s)" % order)
    sys.exit(1)
msgs = order[:short + 1]
ok = True
for k, mid in enumerate(msgs):
    parts = [p for _, m, p in rows if m == mid]
    n = int(parts[0].split("/")[1])
    idx = [j for j, m in enumerate(seq) if m == mid]
    if parts != ["%d/%d" % (j, n) for j in range(1, n + 1)] or idx != list(range(idx[0], idx[0] + n)):
        print("  (message %d: parts %s at lines %s)" % (k + 1, parts, idx))
        ok = False
lines = [r for r in rows if r[1] in msgs]
if len(lines) < 6:
    print("  (%d lines, 6 or more expected)" % len(lines))
    ok = False
# irssi waits 2 s and 1 s per 100 bytes after a line this long
gaps = [b[0] - a[0] for a, b in zip(lines, lines[1:])]
print("  (%d messages, %d lines; gaps: %s)" % (len(msgs), len(lines), " ".join("%.1f" % g for g in gaps)))
if any(g < 0.9 for g in gaps[1:]):
    ok = False
sys.exit(0 if ok else 1)
PY
check "$WORK/bob/logs/E2E/$CHAN.log" "nested from a rawlog hook"
say alice "/script unload e2epumphook"
long_check "$WORK/bob/logs/E2E/$CHAN.log" alice "$LONG_C" "bob reads the paced long message" 60
long_check "$WORK/bob/logs/E2E/$CHAN.log" alice "$LONG_C2" "bob reads the second long message, queued behind the first" 60
# a REKEY reaches bob before the first chunk under the new key
say alice "/e2e rotate"
TIMES_MARK=$(wc -l < "$WORK/wire-times.log")
quick alice "$LONG_D"
long_check "$WORK/bob/logs/E2E/$CHAN.log" alice "$LONG_D" "bob reads the long message after /e2e rotate" 90
python3 - "$WORK/wire-times.log" "$TIMES_MARK" "$CHAN" <<'PY' && pass "after /e2e rotate: the REKEY to bob leaves before the first chunk" || bad "after /e2e rotate: no REKEY to bob before the first chunk"
import sys
path, mark, chan = sys.argv[1], int(sys.argv[2]), sys.argv[3]
rekey = wire = None
for i, l in enumerate(open(path, encoding="utf-8", errors="replace")):
    f = l.rstrip("\n").split(" ", 4)
    if i < mark or len(f) < 5:
        continue
    if rekey is None and f[1:4] == ["alice", "NOTICE", "bob"] and f[4].startswith(":\x01RPEE2E REKEY "):
        rekey = i
    if wire is None and f[1:4] == ["alice", "PRIVMSG", chan] and f[4].startswith(":+RPE2E01 "):
        wire = i
sys.exit(0 if rekey is not None and wire is not None and rekey < wire else 1)
PY

# ---- rpe2e.pl comes while chunks wait in the module's queue ----
# the script encrypts then (and sends its chunks itself), the module's
# queue is not used: what still waits in it is dropped and said, never
# handed to the script or sent later
cat > "$WORK/alice/scripts/rpe2e_hold.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'rpe2e_hold');
Irssi::command_bind('e2e', sub { });
PERL
sleep 5
TIMES_MARK=$(wc -l < "$WORK/wire-times.log")
quick alice "$LONG_G"
if HELD=$(first_wire "$TIMES_MARK" 30); then
    tmux -L "$SOCK" send-keys -t alice -l -- "/script load rpe2e_hold"
    tmux -L "$SOCK" send-keys -t alice Enter
    if wait_for "$WORK/alice/all.log" "not sent: rpe2e.pl is loaded" 10; then
        pass "rpe2e.pl loaded while chunks wait: alice is told they are not sent"
    else
        bad "rpe2e.pl loaded while chunks wait: alice is not told"
    fi
    sbar_hidden alice "alice's #test while rpe2e.pl is loaded: no e2e item"
    # long enough for the rest to leave at the flood queue's pace
    sleep 15
    if wire_groups "$TIMES_MARK" | awk -v m="$HELD" '$1 == m { n = split($2, p, ","); split(p[1], q, "/"); exit !(n < q[2]) }'; then
        pass "rpe2e.pl loaded while chunks wait: the rest of them stays unsent"
    else
        bad "rpe2e.pl loaded while chunks wait: the module's queue was still sent"
    fi
else
    bad "rpe2e.pl loaded while chunks wait: the long message did not start (the check proves nothing)"
fi
say alice "/script unload rpe2e_hold"
sleep 1
sbar_check alice "E2E" "blocked|error" "alice's #test after rpe2e.pl is unloaded: E2E again"
wire_plain "$WORK/wire.log" > "$WORK/wire-plain.log"
leak=0
for word in pacedlong secondlong "short after long" "nested from a rawlog" rotatedlong heldlong; do
    if grep -qiF -- "$word" "$WORK/wire-plain.log"; then bad "plain text crossed the server: $word"; leak=1; fi
done
[ $leak = 0 ] && pass "flood queue: no plain text crossed the server"

# ---- /upgrade with PRIVMSGs in the flood queue ----
# and with the chunks of long messages waiting in the module's queue
# (both cases in one /upgrade, it takes a while): on E2E2 with nothing in irssi's flood
# queue (one PRIVMSG, /quote: irssi does not split it) - its chunks are
# sent before the session is saved; on E2E irssi's queue holds the
# second half of a long line (irssi split it; its chunks follow it at
# once) and three messages behind it
LONG_H=$(python3 -c 'print(" ".join("upgradeonly%d" % i for i in range(1, 30)))')
LONG_F=$(python3 -c 'print(" ".join("upgradelong%d" % i for i in range(1, 61)))')
say alice "/join -E2E2 $CHAN"
say alice "/set cmds_max_at_once 1"
say alice "/set cmd_queue_speed 8s"
sleep 5
TIMES_MARK2=$(wc -l < "$WORK/wire2-times.log")
quick alice "/quote PRIVMSG $CHAN :$LONG_H"
first_wire "$TIMES_MARK2" 30 "$WORK/wire2-times.log" >/dev/null ||
    bad "/upgrade: the long message on E2E2 did not start (the check proves less)"
quick alice "/join -E2E $CHAN"
TIMES_MARK=$(wc -l < "$WORK/wire-times.log")
quick alice "$LONG_F"
first_wire "$TIMES_MARK" 30 >/dev/null || bad "/upgrade: the long message did not start (the check proves less)"
quick alice "upgrade queued one"
quick alice "upgrade queued two"
quick alice "upgrade queued three"
quick alice "/upgrade"
sleep 6
if grep -aqiF "upgrade queued" "$WORK/wire.log"; then
    bad "/upgrade sent queued messages in plain text"
    grep -aF "upgrade queued" "$WORK/wire.log" | sed 's/^/    /'
else
    pass "/upgrade sent no queued message in plain text"
fi
check "$WORK/bob/logs/E2E/$CHAN.log" "upgrade queued three"
long_check "$WORK/bob/logs/E2E/$CHAN.log" alice "$LONG_F" "/upgrade: bob reads the long message whose chunks waited in the queue" 30
if wire_groups "$TIMES_MARK" | python3 -c '
import sys
g = [l.split() for l in sys.stdin]
parts = g[0][1].split(",") if g else []
n = int(parts[0].split("/")[1]) if parts else 0
# the long message whole and first, then the three queued ones
ok = n >= 3 and parts == ["%d/%d" % (j, n) for j in range(1, n + 1)] and len(g) >= 4
ok = ok and all(int(g[0][3]) < int(x[2]) for x in g[1:])
sys.exit(0 if ok else 1)'; then
    pass "/upgrade: the queued chunks left whole, ahead of the messages typed after them"
else
    bad "/upgrade: the queued chunks were lost or overtaken"
    wire_groups "$TIMES_MARK" | sed 's/^/    /'
fi
if wire_groups "$TIMES_MARK2" "$WORK/wire2-times.log" | awk '{ n = split($2, p, ","); split(p[1], q, "/"); exit !(NR == 1 && q[2] >= 2 && n == q[2]) }'; then
    pass "/upgrade with only the module's queue waiting (E2E2): all its chunks left"
else
    bad "/upgrade with only the module's queue waiting (E2E2): chunks lost (or one chunk only: the check proves nothing)"
    wire_groups "$TIMES_MARK2" "$WORK/wire2-times.log" | sed 's/^/    /'
fi
if wire_plain "$WORK/wire.log" | grep -qiF "upgradelong" || wire_plain "$WORK/wire2.log" | grep -qiF "upgradeonly"; then
    bad "/upgrade sent a queued long message in plain text"
else
    pass "/upgrade sent no queued chunk in plain text"
fi
# the end of E2E3's MOTD is kept over /upgrade: still rfc1459 there
say alice "/msg -E2E3 #Żaba rfc1459 plain after upgrade"
if wait_for "$WORK/wire3.log" "alice PRIVMSG #Żaba :rfc1459 plain after upgrade" 20; then
    pass "/upgrade keeps the end of the MOTD: still rfc1459 where no CASEMAPPING was named"
else
    bad "/upgrade lost the end of the MOTD: #Żaba refused as if the fold were Unicode"
fi

# ---- rpe2e.pl keeps what only the module writes ----
# bob's rpe2e.pl loads and saves alice's keyring: "accepted" and
# "seen_rekeys" are the module's own members, rpe2e.pl keeps them
# until now bob's keyring is rpe2e.pl's own (no per-network entries):
# the native module says nothing about it - not in the log, nor on the
# screen of the status window (where it would land while erssi starts)
say bob "/window 1"
if grep -qF "does not know the per-network entries" "$WORK/bob/all.log" ||
   tmux -L "$SOCK" capture-pane -p -J -t bob -S -1000 | grep -qF "does not know the per-network entries"; then
    bad "bob: a downgrade warning on rpe2e.pl's own keyring"
else
    pass "bob: no downgrade warning on rpe2e.pl's own keyring"
fi
cp "$WORK/bob/rpe2e/keyring.json" "$WORK/bob-keyring.json"
cp "$WORK/alice/rpe2e/keyring.json" "$WORK/bob/rpe2e/keyring.json"
say bob "/window goto $CHAN"
# rpe2e.pl on a keyring whose #test is set up only in the module's
# per-network entries ("E2E\x1f#test"): the script would send in clear
# text, the inactive module stops it; a bot command still goes out
WIRE_MARK=$(wc -l < "$WORK/wire.log")
say bob "scoped only from bob"
say bob "!seen scopedbot"
# a wire typed (or pasted) by the user is text, not the script's chunk
TYPED_WIRE=$(grep -a "^alice PRIVMSG $CHAN :+RPE2E01 " "$WORK/wire.log" | tail -n 1 | cut -d' ' -f4- | cut -c2-)
say bob "$TYPED_WIRE"
sleep 1
if [ -n "$TYPED_WIRE" ] && ! grep -aqF "bob PRIVMSG $CHAN :$TYPED_WIRE" "$WORK/wire.log"; then
    pass "rpe2e.pl loaded: a wire typed by the user to that channel is not sent"
else
    bad "rpe2e.pl loaded: a wire typed by the user went out (or none to type)"
fi
if grep -aqF "scoped only from bob" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: a channel only the module encrypts went out in plain text"
else
    pass "rpe2e.pl loaded: a channel only the module encrypts is not sent in plain text"
fi
if wait_for "$WORK/bob/logs/E2E/$CHAN.log" "E2E is on for E2E:$CHAN in the e2e module, which rpe2e.pl does not know" 5; then
    pass "bob is told why it was not delivered"
else
    bad "bob is not told why his message was not delivered"
fi
if grep -aqF "bob PRIVMSG $CHAN :!seen scopedbot" "$WORK/wire.log" &&
   grep -qF "bot-style message to $CHAN sent in CLEARTEXT" "$WORK/bob/logs/E2E/$CHAN.log"; then
    pass "rpe2e.pl loaded: a bot command on that channel still goes out, with the warning"
else
    bad "rpe2e.pl loaded: the bot command was stopped"
fi
say bob "/e2e mode quiet"
# the script decides a raw buffer of several lines by its first target:
# a PRIVMSG behind it to a channel E2E is on for in the script's own
# (bare) entry must not leave in clear either
say bob "/join #multi"
say bob "/e2e on"
say bob "/window goto $CHAN"
say bob "/script exec Irssi::active_server->send_raw('PRIVMSG #pub :multi lead'.chr(13).chr(10).'PRIVMSG #multi :multi secret')"
sleep 1
if grep -aqF "multi secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: a second line of a raw buffer went out in plain text"
else
    pass "rpe2e.pl loaded: a second line of a raw buffer is not sent in plain text"
fi
# rpe2e.pl compares names exactly: #MULTI is #multi to this (ascii)
# server, and E2E is on for #multi in the script's own entry
say bob "/msg #MULTI other case to multi"
sleep 1
if grep -aqF "other case to multi" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: #MULTI (E2E on for #multi in its entry) went out in plain text"
else
    pass "rpe2e.pl loaded: #MULTI (E2E on for #multi in its entry) is not sent in plain text"
fi
if wait_for "$WORK/bob/all.log" "rpe2e.pl compares names exactly" 5; then
    pass "bob is told rpe2e.pl compares names exactly"
else
    bad "bob is not told why #MULTI was not sent"
fi
# a target list: the script looks "#x,#multi" up as one name
say bob "/quote PRIVMSG #x,#multi :list piece to multi"
sleep 1
if grep -aqF "list piece to multi" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: #x,#multi (E2E on for #multi in its entry) went out in plain text"
else
    pass "rpe2e.pl loaded: #x,#multi (E2E on for #multi in its entry) is not sent in plain text"
fi
# the script resolves no query for a nick in a list either
say bob "/query alice"
say bob "/e2e on"
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG alice,carol :script list dm secret')"
sleep 1
if grep -aqF "script list dm secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: alice,carol (E2E on for alice in its entry) went out in plain text"
else
    pass "rpe2e.pl loaded: alice,carol (E2E on for alice in its entry) is not sent in plain text"
fi
# a single nick: the script looks an old bare-nick row up exactly, and
# ZED is zed to this (ascii) server
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the script bare-nick check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["channels"]["zed"] = {"enabled": 1, "mode": "normal"}
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG ZED :script bare nick secret')"
sleep 1
if grep -aqF "script bare nick secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: ZED (E2E on for zed in its entry) went out in plain text"
elif wait_for "$WORK/bob/all.log" "rpe2e.pl does not find it under that name" 5; then
    pass "rpe2e.pl loaded: ZED (E2E on for zed in its entry) is not sent in plain text, and bob is told why"
else
    bad "rpe2e.pl loaded: ZED was not sent, but bob is not told why"
fi
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the script bare-nick check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
del k["channels"]["zed"]
json.dump(k, open(p, "w", encoding="utf-8"))
PY
# a non-ASCII nick the module finds only in the keyring's cache:
# rpe2e.pl 0.2.0 compares last_nick by lc() on the
# decoded string and does not find it (kótek) - not sent in clear, by any
# copy of the script. And a nick
# whose newest address has E2E off while a query of that nick has it on
# (DM moved): refused as the module refuses it
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the cache-only nick checks failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["channels"]["@kt@h9"] = {"enabled": 1, "mode": "normal"}
# stored as the module and the script store irssi's bytes: one character
# per byte ("kÃ³tek")
k["peers"]["9" * 32] = {"pk": "", "last_nick": "kótek".encode("utf-8").decode("latin-1"),
                        "last_handle": "kt@h9", "status": "pending",
                        "first_seen": 1, "last_seen": 1}
k["channels"]["@yan@h1"] = {"enabled": 1, "mode": "normal"}
k["peers"]["1" * 32] = {"pk": "", "last_nick": "yan", "last_handle": "yan@h1", "status": "pending",
                        "first_seen": 1, "last_seen": 1}
k["peers"]["2" * 32] = {"pk": "", "last_nick": "YAN", "last_handle": "other@h2", "status": "pending",
                        "first_seen": 2, "last_seen": 2}
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG kótek :script nonascii secret')"
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG yan :script moved secret')"
sleep 1
# a script that folds nicks as ASCII bytes finds the peer and encrypts;
# 0.2.0 does not, and the module refuses the line
if grep -aqF "script nonascii secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: kótek (E2E on for its query, found only in the keyring) went out in plain text"
elif grep -aqF "bob PRIVMSG kótek :+RPE2E01 " "$WORK/wire.log" ||
     wait_for "$WORK/bob/all.log" "rpe2e.pl cannot find it now" 5; then
    pass "rpe2e.pl loaded: kótek (E2E on for its query, found only in the keyring) is encrypted or refused, not sent in plain text"
else
    bad "rpe2e.pl loaded: kótek was neither encrypted nor refused with a reason"
fi
if grep -aqF "script moved secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: yan (now at another address, E2E on at the old one) went out in plain text"
elif wait_for "$WORK/bob/all.log" "yan is now other@h2" 5; then
    pass "rpe2e.pl loaded: yan (now at another address, E2E on at the old one) is not sent in plain text"
else
    bad "rpe2e.pl loaded: yan was not sent, but bob is not told why"
fi
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the cache-only nick checks failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
for c in ("@kt@h9", "@yan@h1"):
    del k["channels"][c]
for f in ("9" * 32, "1" * 32, "2" * 32):
    del k["peers"][f]
json.dump(k, open(p, "w", encoding="utf-8"))
PY
# two addresses tie for a nick in the keyring (no last_seen): the
# a script that breaks the tie by the smallest fingerprint, as the
# module does, encrypts to the query E2E is on for; 0.2.0
# takes either by Perl's hash order, and the module refuses the line
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the tie check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
k["channels"]["@ti@h1"] = {"enabled": 1, "mode": "normal"}
k["peers"]["a9" + "0" * 30] = {"pk": "", "last_nick": "tia", "last_handle": "ti@h1", "status": "pending"}
k["peers"]["f2" + "0" * 30] = {"pk": "", "last_nick": "tia", "last_handle": "tj@h2", "status": "pending"}
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG tia :script tie secret')"
sleep 1
if grep -aqF "script tie secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: tia (two addresses tie in the keyring) went out in plain text"
elif grep -aqF "bob PRIVMSG tia :+RPE2E01 " "$WORK/wire.log" ||
     wait_for "$WORK/bob/all.log" "may deliver a message to tia" 5; then
    pass "rpe2e.pl loaded: tia (two addresses tie in the keyring) is encrypted or refused, not sent in plain text"
else
    bad "rpe2e.pl loaded: tia was neither encrypted nor refused with a reason"
fi
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the tie check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
del k["channels"]["@ti@h1"]
for f in ("a9", "f2"):
    del k["peers"][f + "0" * 30]
json.dump(k, open(p, "w", encoding="utf-8"))
PY
# a bot command bypasses E2E on a channel only; the channel piece of a
# list does not make the DM piece a bot command
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG #x,alice :!help script bypass secret')"
say bob "/script exec Irssi::server_find_tag('E2E')->send_raw('PRIVMSG alice,#x :.net script bypass secret')"
sleep 1
if grep -aqF "script bypass secret" "$WORK/wire.log"; then
    bad "rpe2e.pl loaded: a bot command to #x,alice (E2E on for alice in its entry) went out in plain text"
else
    pass "rpe2e.pl loaded: a bot command to #x,alice (E2E on for alice in its entry) is not sent in plain text"
fi
say bob "/e2e off"
say bob "/window close"
sleep 1
python3 - "$WORK/bob/rpe2e/keyring.json" <<'PY' && pass "bob: E2E for alice is off again after the list checks" || bad "bob: E2E for alice is left on after the list checks"
import json, sys
b = json.load(open(sys.argv[1], encoding="utf-8"))
assert not b["channels"]["@alice@127.0.0.1"].get("enabled"), "still on"
PY
python3 - "$WORK/alice/rpe2e/keyring.json" "$WORK/bob/rpe2e/keyring.json" "$CHAN" <<'PY' && pass "rpe2e.pl loads and saves the module's keyring, its own members kept" || bad "rpe2e.pl lost what the module stored"
import json, sys
a, b = (json.load(open(p, encoding="utf-8")) for p in sys.argv[1:3])
assert b["channels"][sys.argv[3]]["mode"] == "quiet", "rpe2e.pl did not save"
assert a.get("accepted"), "alice accepted nobody"
assert b.get("accepted") == a["accepted"], "accepted changed"
assert b.get("seen_rekeys") == a.get("seen_rekeys"), "seen_rekeys changed"
assert b["identity"] == a["identity"]
PY
# the downgrade: rpe2e.pl on a keyring with per-network entries cannot
# see them - the native module (inactive) warns
say bob "after copy $LONG_A"
if wait_for "$WORK/bob/all.log" "does not know the per-network entries" 5; then
    pass "bob: the native module warns that rpe2e.pl cannot see the per-network entries"
else
    bad "bob: no downgrade warning with rpe2e.pl on a scoped keyring"
fi
# #test is on in rpe2e.pl's own (bare) entry now: the script encrypts,
# and its chunks pass the module
sleep 2
if tail -n +"$((WIRE_MARK + 1))" "$WORK/wire.log" | python3 -c '
import sys
lines = [l for l in sys.stdin if l.startswith("bob PRIVMSG #test :")]
wires = [l for l in lines if l.startswith("bob PRIVMSG #test :+RPE2E01 ")]
clear = [l for l in lines if "after copy" in l or "zażółć" in l]
sys.exit(0 if len(wires) >= 2 and not clear else 1)'; then
    pass "rpe2e.pl loaded: its own encrypted chunks still go out"
else
    bad "rpe2e.pl loaded: the script's chunks were dropped or plain text went out"
fi

# ---- the signals of e2e-web.c: the state, and a frontend's accept/decline ----
# a script stands in for a frontend: it logs every "e2e state" and every
# "e2e action refused" (what a frontend would answer its client with), and
# emits a request or an action as a frontend would
cat > "$WORK/alice/scripts/e2eweb.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'e2eweb');
Irssi::signal_register({
    'e2e state' => [qw(Irssi::Server string string)],
    'e2e state request' => [qw(Irssi::Server string)],
    'e2e action' => [qw(Irssi::Server string string string string)],
    'e2e action refused' => [qw(Irssi::Server string string string)],
});
my $log = Irssi::get_irssi_dir() . '/e2e-state.log';
my $refused_log = Irssi::get_irssi_dir() . '/e2e-refused.log';
Irssi::signal_add('e2e action refused', sub {
    my ($server, $target, $handle, $reason) = @_;
    open(my $fh, '>>', $refused_log) or return;
    print $fh (($server ? $server->{tag} : '-') . " $target $handle " .
               (defined $reason ? $reason : '(none)') . "\n");
    close $fh;
});
Irssi::signal_add('e2e state', sub {
    my ($server, $target, $json) = @_;
    open(my $fh, '>>', $log) or return;
    print $fh (($server ? $server->{tag} : '-') . " $target $json\n");
    close $fh;
});
Irssi::command_bind('e2etest_state', sub {
    my ($data, $server) = @_;
    my ($target) = split ' ', $data;
    Irssi::signal_emit('e2e state request', $server, $target);
});
Irssi::command_bind('e2etest_action', sub {
    my ($data, $server) = @_;
    my ($target, $action, $handle, $fp) = split ' ', $data;
    if (defined $fp) {
        Irssi::signal_emit('e2e action', $server, $target, $action, $handle, $fp);
    } else {
        Irssi::signal_emit('e2e action', $server, $target, $action, $handle);
    }
});
PERL
cp "$WORK/alice/scripts/e2eweb.pl" "$WORK/bob/scripts/e2eweb.pl"
# bob runs rpe2e.pl: the module is inactive - its state is "off" with no
# requests (the script's is not the module's to tell), and an action is
# refused without a look at the keyring
say bob "/script load e2eweb"
say bob "/window goto $CHAN"
say bob "/e2etest_state $CHAN"
if wait_for "$WORK/bob/e2e-state.log" "E2E $CHAN {\"v\":1,\"state\":\"off\"" 10 &&
   ! grep -aqF '"pending":[{' "$WORK/bob/e2e-state.log"; then
    pass "web, rpe2e.pl loaded: the module reports off, no requests"
else
    bad "web, rpe2e.pl loaded: the module reports something else (log: $(tail -n 1 "$WORK/bob/e2e-state.log" 2>/dev/null))"
fi
BOB_KR=$(md5sum < "$WORK/bob/rpe2e/keyring.json")
WIRE_MARK=$(wc -l < "$WORK/wire.log")
say bob "/e2etest_action $CHAN accept alice@127.0.0.1"
sleep 2
if grep -aqF "a frontend asked to accept alice@127.0.0.1 on $CHAN, but the native e2e module is inactive while rpe2e.pl is loaded — nothing sent" "$WORK/bob/all.log" &&
   [ "$(md5sum < "$WORK/bob/rpe2e/keyring.json")" = "$BOB_KR" ] &&
   ! tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^bob NOTICE alice :.RPEE2E KEYRSP" &&
   grep -aqxF "E2E $CHAN alice@127.0.0.1 inactive" "$WORK/bob/e2e-refused.log"; then
    pass "web action, rpe2e.pl loaded: refused (inactive), keyring untouched"
else
    bad "web action, rpe2e.pl loaded: not refused, or the keyring changed"
fi
say bob "/script unload e2eweb"

# ---- one network, an old keyring: nothing to do ----
# bob unloads rpe2e.pl, his own (unscoped) keyring comes back, and the
# native module takes it over with the first line he sends (incoming
# traffic never does): he reads alice and she reads him without a new
# key exchange
say bob "/script unload rpe2e"
sleep 1
cp "$WORK/bob-keyring.json" "$WORK/bob/rpe2e/keyring.json"
say bob "/window goto $CHAN"
# the old (unscoped) entry is good for the one network in use: E2E; a
# second network bob would connect to makes it BLOCKED (the gate refuses
# then), as soon as the server is added - and E2E again when it is gone
sbar_check bob "E2E" "waiting|blocked|error" "bob's #test on an old keyring, one network: E2E"
say bob "/server add -auto 192.0.2.1 6667"
sbar_check bob "E2E blocked" "" "bob's #test with a second network set to autoconnect: E2E blocked"
say bob "/server remove 192.0.2.1 6667"
sbar_check bob "E2E" "waiting|blocked|error" "bob's #test with that server removed: E2E again"
say bob "the native module on an old keyring"
check "$WORK/alice/logs/E2E/$CHAN.log" "the native module on an old keyring"
say alice "/join -E2E $CHAN"
say alice "alice to the old keyring"
check "$WORK/bob/logs/E2E/$CHAN.log" "alice to the old keyring"
if grep -aqF "old keyring" "$WORK/wire.log"; then
    bad "plain text after the native module took bob's keyring over"
else
    pass "no plain text after the native module took bob's keyring over"
fi
python3 - "$WORK/bob-keyring.json" "$WORK/bob/rpe2e/keyring.json" "$CHAN" <<'PY' && pass "bob: his old #test entries are the network's now, the keys the same" || bad "bob: the old keyring was not taken over as it is"
import json, sys
old, new = (json.load(open(p, encoding="utf-8")) for p in sys.argv[1:3])
c = sys.argv[3]
s = "E2E\x1f" + c
assert new["outgoing"][s]["sk"] == old["outgoing"][c]["sk"], "our key changed"
assert new["incoming"]["alice@127.0.0.1|" + s]["sk"] == old["incoming"]["alice@127.0.0.1|" + c]["sk"]
assert new["channels"][s]["enabled"]
assert new["channels"][s]["adopted"] == "E2E" and new["outgoing"][s]["adopted"] == "E2E"
assert c not in new["channels"] and c not in new["outgoing"]
assert "alice@127.0.0.1|" + c not in new["incoming"]
assert new["identity"] == old["identity"]
PY

# ---- a script of rpe2e.pl's name dies inside its gate ----
# loaded first, then the module (/unload, /load): the module's gate runs
# ahead of the script's, at the same priority, while the module is
# inactive. The script dies there and irssi unloads it at once, so the
# module is active again when its last hook (the rpe2e.pl guard) runs -
# the line has met no gate yet and must not go out in clear text
cat > "$WORK/alice/scripts/rpe2e_die.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'rpe2e_die');
Irssi::command_bind('e2e', sub { });
Irssi::signal_add_first('server outgoing modify', sub {
    my ($server, $line) = @_;    # $line: a reference to the line
    die "rpe2e_die: dies in its gate\n" if $$line =~ /dies in the gate/;
});
PERL
# the flood queue is slow since the /upgrade check: back to the defaults
say alice "/set -default cmds_max_at_once"
say alice "/set -default cmd_queue_speed"

# ---- a handler rewrites a line after the module's gate let it pass ----
# the module's last hook sees another line than its gate did:
# it decides that one too
cat > "$WORK/alice/scripts/e2erewrite.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'e2erewrite');
# -99: after the module's gate (-100), before every default handler
Irssi::signal_add_priority('server outgoing modify', sub {
    my ($server, $line) = @_;    # $line: a reference to the line
    $$line = "PRIVMSG #test :a rewritten secret line" if $$line =~ /^PRIVMSG #pub :rewrite me/;
}, -99);
PERL
say alice "/join -E2E $CHAN"
say alice "/script load e2erewrite"
say alice "/msg -E2E #pub rewrite me"
# the flood queue may hold it a while: once bob has it, it was sent
check "$WORK/bob/logs/E2E/$CHAN.log" "a rewritten secret line"
if grep -aqF "a rewritten secret line" "$WORK/wire.log"; then
    bad "a line rewritten after the module's gate went out in plain text"
else
    pass "a line rewritten after the module's gate is gated again"
fi
say alice "/script unload e2erewrite"
# the same with a script that hooks in right after the module's gate,
# and no reload: the module decided earlier lines while it was active,
# never this one
cat > "$WORK/alice/scripts/rpe2e_die2.pl" <<'PERL'
use strict;
use Irssi;
our %IRSSI = (name => 'rpe2e_die2');
Irssi::command_bind('e2e', sub { });
# -99: after the module's gate (-100), before every default handler
Irssi::signal_add_priority('server outgoing modify', sub {
    my ($server, $line) = @_;    # $line: a reference to the line
    die "rpe2e_die2: dies after the module's gate\n" if $$line =~ /dies right after/;
}, -99);
PERL
say alice "/join -E2E $CHAN"
say alice "an encrypted line before the script"
say alice "/script load rpe2e_die2"
say alice "a secret line the script dies right after the gate for"
if wait_for "$WORK/alice/all.log" "rpe2e_die2: dies after the module's gate" 15; then
    sleep 1
    pass "the script hooked in after the module's gate died there"
else
    bad "the script hooked in after the module's gate did not die (the check proves nothing)"
fi
if grep -aqF "dies right after the gate for" "$WORK/wire.log"; then
    bad "a line went out in plain text when the script died right after the module's gate"
else
    pass "no plain text when the script dies right after the module's gate"
fi
check "$WORK/bob/logs/E2E/$CHAN.log" "a secret line the script dies right after the gate for"
say alice "/script load rpe2e_die"
say alice "/unload e2e"
say alice "/load e2e"
say alice "/join -E2E $CHAN"
say alice "a secret line the script dies in the gate for"
if wait_for "$WORK/alice/all.log" "rpe2e_die: dies in its gate" 15; then
    sleep 1
    pass "the script of rpe2e.pl's name died in its gate"
else
    bad "the script of rpe2e.pl's name did not die in its gate (the check proves nothing)"
fi
if grep -aqF "dies in the gate for" "$WORK/wire.log"; then
    bad "a line went out in plain text when the script died in its gate"
else
    pass "no plain text when the script dies in its gate"
fi
check "$WORK/bob/logs/E2E/$CHAN.log" "a secret line the script dies in the gate for"

# ---- /e2e decline and accept by address ----
# mallory asks for alice's key of their query too: the prompt says to
# answer from any window of the network
say alice "/query mallory"
say alice "/e2e on"
say alice "/window close"
say mallory "/query alice"
say mallory "/e2e on"
say mallory "/e2e handshake alice"
if wait_for "$WORK/alice/all.log" "Pending key exchange from mallory (mallory@127.0.0.1) for a query" 20 &&
   grep -aF "Pending key exchange from mallory (mallory@127.0.0.1) for a query" "$WORK/alice/all.log" |
   grep -qF "Run /e2e accept mallory@127.0.0.1 or /e2e decline mallory@127.0.0.1 in any window of E2E"; then
    pass "alice: mallory's query request, answered from any window of E2E"
else
    bad "alice: no prompt, or not the query prompt, for mallory's query request"
fi
say alice "/join -E2E $CHAN"

# ---- a frontend's accept/decline, through the signals of e2e-web.c ----
STATE_LOG=$WORK/alice/e2e-state.log
state_lines() {  # state_lines <mark> <ERE>: lines for E2E's #test after line <mark> matching
    tail -n +$(($1 + 1)) "$STATE_LOG" 2>/dev/null | grep -aF "E2E $CHAN {" | grep -aE -- "$2"
}
wait_state() {  # wait_state <mark> <ERE> <ERE not there, or ""> <seconds>
    local n=0
    while [ $n -lt "$4" ]; do
        if [ -n "$3" ]; then
            state_lines "$1" "$2" | grep -avqE -- "$3" && return 0
        else
            state_lines "$1" "$2" >/dev/null && return 0
        fi
        sleep 1; n=$((n + 1))
    done
    return 1
}
say alice "/script load e2eweb"
say alice "/e2etest_state $CHAN"
# what the module refused since line $1 of the refusal log (none: empty)
REFUSED_LOG="$WORK/alice/e2e-refused.log"
refused_since() {
    [ -f "$REFUSED_LOG" ] && tail -n +$(($1 + 1)) "$REFUSED_LOG"
    return 0
}
refused_mark() {
    if [ -f "$REFUSED_LOG" ]; then wc -l < "$REFUSED_LOG"; else echo 0; fi
}
if wait_state 0 '"v":1,"state":"on"' "" 10 &&
   state_lines 0 '"state":"on"' | grep -aqF '"handle":"mallory@127.0.0.1"'; then
    pass "web: the state of #test (on) lists mallory's request"
else
    bad "web: no state of #test with mallory's request (log: $(tail -n 2 "$STATE_LOG" 2>/dev/null))"
fi
# a request that does not wait is never acted on: nothing to nobody
WIRE_MARK=$(wc -l < "$WORK/wire.log")
say alice "/e2etest_action $CHAN accept nobody@127.0.0.1"
sleep 2
if grep -aqF "a frontend asked to accept nobody@127.0.0.1 on $CHAN, but no such request waits — nothing sent" "$WORK/alice/logs/E2E/$CHAN.log" &&
   ! tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE nobody" &&
   refused_since 0 | grep -aqxF "E2E $CHAN nobody@127.0.0.1 no_request"; then
    pass "web accept of a stale request: refused (no_request), nothing sent"
else
    bad "web accept of a stale request: not refused, or alice sent nobody a NOTICE"
fi
# a nick is no ident@host: the request's own address only (a script can
# emit the signal with anything)
WIRE_MARK=$(wc -l < "$WORK/wire.log")
say alice "/e2etest_action $CHAN accept mallory"
sleep 2
if grep -aqF "a frontend asked to accept mallory on $CHAN, but no such request waits — nothing sent" "$WORK/alice/logs/E2E/$CHAN.log" &&
   ! tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE mallory" &&
   refused_since 0 | grep -aqxF "E2E $CHAN mallory no_request"; then
    pass "web accept by nick: refused (no_request), nothing sent (only the exact ident@host)"
else
    bad "web accept by nick: not refused, or alice sent mallory something"
fi
# decline: #test's request only (the window's), then a new state
MARK=$(wc -l < "$STATE_LOG")
DECLINED_BEFORE=$(grep -acF "declined mallory (mallory@127.0.0.1) on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log")
REFUSED_MARK=$(refused_mark)
say alice "/e2etest_action $CHAN decline mallory@127.0.0.1"
if wait_state "$MARK" '"state":"on"' 'mallory' 10 &&
   grep -aqF "frontend: decline mallory@127.0.0.1 on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log" &&
   [ "$(grep -acF "declined mallory (mallory@127.0.0.1) on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log")" -gt "$DECLINED_BEFORE" ] &&
   [ -z "$(refused_since "$REFUSED_MARK")" ]; then
    pass "web decline of mallory in #test: declined (not refused), a new state without mallory"
else
    bad "web decline of mallory in #test: not declined, or no new state without him"
fi
# the web names the window's request only: mallory's query request (an
# /e2e accept of the address in #test would take it) is not touched
WIRE_MARK=$(wc -l < "$WORK/wire.log")
REFUSED_MARK=$(refused_mark)
say alice "/e2etest_action $CHAN accept mallory@127.0.0.1"
sleep 2
if grep -aqF "a frontend asked to accept mallory@127.0.0.1 on $CHAN, but no such request waits — nothing sent" "$WORK/alice/logs/E2E/$CHAN.log" &&
   ! tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE mallory" &&
   refused_since "$REFUSED_MARK" | grep -aqxF "E2E $CHAN mallory@127.0.0.1 no_request"; then
    pass "web accept in #test never takes mallory's query request (no_request)"
else
    bad "web accept in #test took another request of mallory, or sent him something"
fi
# a request that comes in: a new state, unasked
# (alice drops a second KEYREQ of one sender and context within 10 s:
# mallory's client also asks by itself when alice writes)
MARK=$(wc -l < "$STATE_LOG")
say mallory "/window goto $CHAN"
for try in 1 2; do
    sleep 11
    MALLORY_REQ_AT=$(date +%s)
    say mallory "/e2e handshake alice"
    wait_state "$MARK" '"handle":"mallory@127.0.0.1"' "" 20 && break
done
if wait_state "$MARK" '"handle":"mallory@127.0.0.1"' "" 1; then
    pass "web: mallory's new request comes as a new state"
else
    bad "web: mallory's new request did not come as a state"
fi
# in #test the address names #test's request first
DECLINED_BEFORE=$(grep -acF "declined mallory (mallory@127.0.0.1) on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log")
say alice "/e2e decline mallory@127.0.0.1"
n=0
while [ "$(grep -acF "declined mallory (mallory@127.0.0.1) on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log")" -le "$DECLINED_BEFORE" ] && [ $n -lt 5 ]; do
    sleep 1; n=$((n + 1))
done
if [ "$(grep -acF "declined mallory (mallory@127.0.0.1) on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log")" -gt "$DECLINED_BEFORE" ]; then
    pass "/e2e decline mallory@127.0.0.1 declines mallory's request"
else
    bad "/e2e decline mallory@127.0.0.1 did not decline"
fi
# then, with none for #test, the request of the query with that address -
# never #test's session of mallory
say alice "/e2e accept mallory@127.0.0.1"
if wait_for "$WORK/alice/logs/E2E/$CHAN.log" "accepted mallory (mallory@127.0.0.1) on E2E:@mallory@127.0.0.1 — KEYRSP sent" 5 &&
   ! grep -qF "accepted mallory (mallory@127.0.0.1) on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log"; then
    pass "/e2e accept mallory@127.0.0.1 in #test answers mallory's query request, not #test"
else
    bad "/e2e accept mallory@127.0.0.1 in #test did not take the query request"
    grep -aF "accepted mallory" "$WORK/alice/logs/E2E/$CHAN.log" | sed 's/^/    /'
fi
say alice "/e2e pending"
if wait_for "$WORK/alice/all.log" "No key requests wait for /e2e accept." 5; then
    pass "/e2e pending: no request waits after the decline"
else
    bad "/e2e pending: requests still wait after the decline"
    grep -aF " ago — /e2e accept " "$WORK/alice/all.log" | tail -n 5 | sed 's/^/    /'
fi

# ---- a damaged entry: another address accepted for bob's query ----
# (what rpe2e.pl's request for another person's query left behind)
# it gets no REKEY of the query, is listed as damaged, and /e2e decline
# removes it
python3 - "$WORK/alice/rpe2e/keyring.json" <<'PY' || bad "the keyring edit for the damaged-entry check failed"
import json, sys
p = sys.argv[1]
k = json.load(open(p, encoding="utf-8"))
c = "E2E\x1f@bob@127.0.0.1"
fp = next(f for f, r in k["peers"].items() if r.get("last_handle") == "mallory@127.0.0.1")
assert c in k["outgoing"]
k["accepted"]["mallory@127.0.0.1|" + c] = {"fp": fp, "accepted_at": 1, "by": "accept"}
k["outgoing_recipients"][c + "|mallory@127.0.0.1"] = {"channel": c, "handle": "mallory@127.0.0.1",
                                                      "fingerprint": fp, "first_sent_at": 1}
json.dump(k, open(p, "w", encoding="utf-8"))
PY
say alice "/query bob"
# no /e2e rotate, no /e2e decline: mallory may hold the current key of
# bob's query, so the next message rotates it - the REKEY reaches bob only
WIRE_MARK=$(wc -l < "$WORK/wire.log")
say alice "dm after a damaged entry"
# the flood queue may still hold earlier lines: wait for the message
n=0
until tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice PRIVMSG bob :+RPE2E01 " || [ $n -ge 60 ]; do
    sleep 1; n=$((n + 1))
done
sleep 2
if ! tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE bob :.RPEE2E REKEY "; then
    bad "damaged entry: the key of bob's query did not rotate by itself"
elif tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE mallory :.RPEE2E REKEY "; then
    bad "damaged entry: mallory got the REKEY of bob's query"
else
    pass "damaged entry: the key of bob's query rotates by itself, the REKEY reaches bob only"
fi
if grep -qF "a damaged entry: mallory@127.0.0.1 was accepted for the query E2E:@bob@127.0.0.1" "$WORK/alice/logs/E2E/bob.log"; then
    pass "damaged entry: alice is told"
else
    bad "damaged entry: alice is not told"
fi
say alice "/e2e list"
if wait_for "$WORK/alice/logs/E2E/bob.log" "damaged: mallory@127.0.0.1 on E2E:@bob@127.0.0.1" 5; then
    pass "/e2e list in bob's query shows mallory's entry as damaged"
else
    bad "/e2e list in bob's query does not show the damaged entry"
fi
say alice "/e2e decline mallory@127.0.0.1"
if wait_for "$WORK/alice/logs/E2E/bob.log" "removed 1 damaged entry on E2E:@bob@127.0.0.1" 5 &&
   python3 -c 'import json, sys; k = json.load(open(sys.argv[1], encoding="utf-8")); sys.exit("mallory@127.0.0.1|E2E\x1f@bob@127.0.0.1" in k["accepted"])' "$WORK/alice/rpe2e/keyring.json"; then
    pass "/e2e decline mallory@127.0.0.1 in bob's query removes the damaged entry"
else
    bad "/e2e decline mallory@127.0.0.1 in bob's query did not remove the damaged entry"
fi
say alice "/window close"

# ---- a frontend's accept, bound to the key it showed ----
# #plain (E2E off) is told through a request: it gets "off" on /unload too
say alice "/join -E2E #plain"
say alice "/e2etest_state #plain"
say alice "/join -E2E $CHAN"
MARK=$(wc -l < "$STATE_LOG")
say mallory "/window goto $CHAN"
# mallory's own request of the earlier check waits 120 s on his side:
# no new one before it expires
now=$(date +%s)
[ $((MALLORY_REQ_AT + 125 - now)) -gt 0 ] && sleep $((MALLORY_REQ_AT + 125 - now))
for try in 1 2; do
    sleep 11
    say mallory "/e2e handshake alice"
    wait_state "$MARK" '"handle":"mallory@127.0.0.1"' "" 20 && break
done
FP=$(state_lines "$MARK" '"handle":"mallory@127.0.0.1"' | tail -n 1 |
     grep -o '"handle":"mallory@127.0.0.1","fingerprint":"[0-9a-f]*"' | sed 's/.*"fingerprint":"//; s/"$//')
if [ ${#FP} -eq 16 ]; then
    pass "web: mallory's request again, with the fingerprint of his key"
else
    bad "web: mallory's request did not come again with a fingerprint (the checks below prove nothing)"
fi
WRONG=0000000000000000
[ "$FP" = "$WRONG" ] && WRONG=ffffffffffffffff
WIRE_MARK=$(wc -l < "$WORK/wire.log")
REFUSED_MARK=$(refused_mark)
say alice "/e2etest_action $CHAN accept mallory@127.0.0.1 $WRONG"
sleep 2
MARK=$(wc -l < "$STATE_LOG")
say alice "/e2etest_state $CHAN"
if grep -aqF "a frontend asked to accept mallory@127.0.0.1 on $CHAN for the key $WRONG, but the request that waits has the key $FP — nothing sent" "$WORK/alice/logs/E2E/$CHAN.log" &&
   ! tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE mallory" &&
   wait_state "$MARK" '"handle":"mallory@127.0.0.1"' "" 5 &&
   refused_since "$REFUSED_MARK" | grep -aqxF "E2E $CHAN mallory@127.0.0.1 mismatch"; then
    pass "web accept with another key's fingerprint: refused (mismatch), nothing sent, the request still waits"
else
    bad "web accept with another key's fingerprint: not refused, or something sent"
fi
# the fingerprint the state showed: accepted, the KEYRSP goes to mallory
WIRE_MARK=$(wc -l < "$WORK/wire.log")
MARK=$(wc -l < "$STATE_LOG")
REFUSED_MARK=$(refused_mark)
say alice "/e2etest_action $CHAN accept mallory@127.0.0.1 $FP"
n=0
until tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE mallory :.RPEE2E KEYRSP" || [ $n -ge 10 ]; do
    sleep 1; n=$((n + 1))
done
if tail -n +$((WIRE_MARK + 1)) "$WORK/wire.log" | grep -aq "^alice NOTICE mallory :.RPEE2E KEYRSP" &&
   grep -aqF "frontend: accept mallory@127.0.0.1 on E2E:$CHAN" "$WORK/alice/logs/E2E/$CHAN.log" &&
   wait_state "$MARK" '"state":"on"' 'mallory' 10 &&
   [ -z "$(refused_since "$REFUSED_MARK")" ]; then
    pass "web accept with the shown fingerprint: KEYRSP to mallory (not refused), a new state without his request"
else
    bad "web accept with the shown fingerprint: no KEYRSP to mallory, or his request still listed"
fi

# ---- /unload e2e with lines waiting ----
# a message to an encrypted channel still in irssi's flood queue would
# leave in clear text without the module: dropped and said, with no
# "0 encrypted line(s)" when the module's own queue is empty
say alice "/join -E2E $CHAN"
sleep 9
UNLOAD_MARK=$(wc -l < "$STATE_LOG")
quick alice "unload short one"
quick alice "unload short two"
quick alice "/unload e2e"
if wait_for "$WORK/alice/all.log" "message(s) to E2E not sent: the e2e module is unloaded (not encrypted yet)" 10 &&
   ! grep -qF "0 encrypted line(s)" "$WORK/alice/all.log"; then
    pass "/unload e2e with a message in irssi's queue: alice is told it is not sent"
else
    bad "/unload e2e with a message in irssi's queue: alice is not told, or told of 0 encrypted lines"
fi
# every item a frontend was told about is told "off": #test, and
# #plain, told through a request only
if tail -n +$((UNLOAD_MARK + 1)) "$STATE_LOG" | grep -aqF "E2E $CHAN {\"v\":1,\"state\":\"off\"" &&
   tail -n +$((UNLOAD_MARK + 1)) "$STATE_LOG" | grep -aqF "E2E #plain {\"v\":1,\"state\":\"off\""; then
    pass "web: /unload e2e tells every item it told about \"off\""
else
    bad "web: /unload e2e left an item a frontend was told about without \"off\""
fi
say alice "/load e2e"
sleep 2
# the chunks in the module's queue are dropped and said; the rest of
# the long line, which irssi split and still holds in its flood queue as
# typed, is not sent either; a message to a channel without E2E behind
# it is not the module's to stop: it still goes out
LONG_U=$(python3 -c 'print(" ".join("unloadlong%d" % i for i in range(1, 61)))')
say alice "/join -E2E $CHAN"
sleep 9
TIMES_MARK=$(wc -l < "$WORK/wire-times.log")
quick alice "$LONG_U"
if UNLOADED=$(first_wire "$TIMES_MARK" 30); then
    quick alice "/msg -E2E #pub kept after unload"
    quick alice "/unload e2e"
    # the module's chunks, and the second half of LONG_U from irssi's
    # queue - not the message to #pub
    if wait_for "$WORK/alice/all.log" "encrypted line(s) to E2E not sent: the e2e module is unloaded (and 1 message(s) waiting behind them, not encrypted yet)" 10; then
        pass "/unload e2e with queued chunks: alice is told they are not sent"
    else
        bad "/unload e2e with queued chunks: alice is not told"
    fi
    sleep 8
    if wire_groups "$TIMES_MARK" | awk -v m="$UNLOADED" '$1 == m { n = split($2, p, ","); split(p[1], q, "/"); exit !(n < q[2]) }'; then
        pass "/unload e2e with queued chunks: the rest of them is not sent"
    else
        bad "/unload e2e with queued chunks: all of them were sent (the check proves nothing)"
    fi
else
    bad "/unload e2e with queued chunks: the long message did not start (the check proves nothing)"
fi
if wire_plain "$WORK/wire.log" | grep -qiF -e "unloadlong" -e "unload short"; then
    bad "/unload e2e with queued chunks: plain text crossed the server"
else
    pass "/unload e2e with queued chunks: no plain text crossed the server"
fi
if wait_for "$WORK/wire.log" "alice PRIVMSG #pub :kept after unload" 30; then
    pass "/unload e2e: a message the module lets pass (no E2E there) still goes out"
else
    bad "/unload e2e: a message to a channel without E2E was dropped"
fi
say alice "/load e2e"
sleep 2

# ---- disconnect with chunks in the module's queue ----
# the rest is dropped and said, never sent
LONG_E=$(python3 -c 'print(" ".join("droppedlong%d" % i for i in range(1, 61)))')
say alice "/join -E2E $CHAN"
sleep 5
TIMES_MARK=$(wc -l < "$WORK/wire-times.log")
quick alice "$LONG_E"
if DROPPED=$(first_wire "$TIMES_MARK" 30); then
    quick alice "/disconnect E2E"
    if wait_for "$WORK/alice/all.log" "not sent: disconnected" 10; then
        pass "disconnect with queued chunks: alice is told they are not sent"
    else
        bad "disconnect with queued chunks: alice is not told"
    fi
    sleep 2
    if wire_groups "$TIMES_MARK" | awk -v m="$DROPPED" '$1 == m { n = split($2, p, ","); split(p[1], q, "/"); exit !(n < q[2]) }'; then
        pass "disconnect with queued chunks: the rest of them is not sent"
    else
        bad "disconnect with queued chunks: all of them were sent (the check proves nothing)"
    fi
else
    bad "disconnect with queued chunks: the long message did not start (the check proves nothing)"
fi
if wire_plain "$WORK/wire.log" | grep -qiF "droppedlong"; then
    bad "disconnect with queued chunks: plain text crossed the server"
else
    pass "disconnect with queued chunks: no plain text crossed the server"
fi

# ---- /e2e reset with chunks, and a message typed meanwhile, waiting ----
# (on E2E2, last: alice gets a new identity) the keyring turns out
# corrupt while the chunks of a long message wait and a message is typed
# behind them; /e2e reset drops all of it - E2E is off everywhere after
# the reset, that message would leave in clear text
LONG_R=$(python3 -c 'print(" ".join("resetlong%d" % i for i in range(1, 61)))')
say alice "/join -E2E2 $CHAN"
sleep 5
TIMES_MARK=$(wc -l < "$WORK/wire2-times.log")
quick alice "$LONG_R"
if RESETID=$(first_wire "$TIMES_MARK" 30 "$WORK/wire2-times.log"); then
    printf 'not json' > "$WORK/alice/rpe2e/keyring.json"
    quick alice "/e2e status"
    quick alice "typed while the keyring is lost"
    quick alice "/e2e reset"
    if wait_for "$WORK/alice/all.log" "still waiting to be sent (messages typed while the keyring was lost, parts of encrypted messages) were dropped" 10; then
        pass "/e2e reset with queued chunks: alice is told they are dropped"
    else
        bad "/e2e reset with queued chunks: alice is not told"
    fi
    # long enough for the rest to leave at the flood queue's pace (8 s)
    sleep 20
    if wire_groups "$TIMES_MARK" "$WORK/wire2-times.log" | awk -v m="$RESETID" '$1 == m { n = split($2, p, ","); split(p[1], q, "/"); exit !(n < q[2]) }'; then
        pass "/e2e reset with queued chunks: the rest of them is not sent"
    else
        bad "/e2e reset with queued chunks: all of them were sent (the check proves nothing)"
    fi
else
    bad "/e2e reset with queued chunks: the long message did not start (the check proves nothing)"
fi
if wire_plain "$WORK/wire2.log" | grep -qiF -e "resetlong" -e "typed while the keyring is lost"; then
    bad "/e2e reset with queued chunks: plain text crossed the server"
else
    pass "/e2e reset with queued chunks: no plain text crossed the server"
fi

# ---- the module loaded for this session only (last: erin upgrades) ----
# "e2e" missing from autoload_modules: a restart or /upgrade does not load
# the module again, and E2E targets would get clear text. Each step says
# so: /load, /e2e on, /unload with E2E on, /upgrade with E2E on.
SESSION_ONLY='the e2e module is loaded for this session only'
if grep -aqF "$SESSION_ONLY" "$WORK/alice/all.log"; then
    bad "alice (e2e in autoload_modules) is warned that the module is loaded for this session only"
else
    pass "alice (e2e in autoload_modules): no session-only warning on /load"
fi
say erin "/set autoload_modules irc dcc flood notifylist perl otr fe_web anti_floodnet"
say erin "/unload e2e"
if grep -aqF "messages there are now sent in clear text" "$WORK/erin/all.log"; then
    bad "/unload e2e with E2E off everywhere: erin is warned of clear text"
else
    pass "/unload e2e with E2E off everywhere: no clear-text warning"
fi
say erin "/load e2e"
if wait_for "$WORK/erin/all.log" "$SESSION_ONLY" 10 &&
   grep -aqF "/SET autoload_modules irc dcc flood notifylist perl otr fe_web anti_floodnet e2e" "$WORK/erin/all.log"; then
    pass "/load e2e without e2e in autoload_modules: erin is told, with the /SET to run"
else
    bad "/load e2e without e2e in autoload_modules: erin is not told"
fi
say erin "/window goto $CHAN"
say erin "/e2e on"
sleep 1
if [ "$(grep -acF "$SESSION_ONLY" "$WORK/erin/all.log")" -ge 2 ]; then
    pass "/e2e on without e2e in autoload_modules: erin is told again"
else
    bad "/e2e on without e2e in autoload_modules: erin is not told again"
fi
say erin "/unload e2e"
if wait_for "$WORK/erin/all.log" "E2E is on for 1 channel or query, and messages there are now sent in clear text (/load e2e encrypts them again)" 10; then
    pass "/unload e2e with E2E on: erin is told that messages now go out in clear text"
else
    bad "/unload e2e with E2E on: erin is not told about clear text"
fi
# the keyring lost (a corrupt one moved aside): the module refused every
# message, which targets have E2E on is not known - /unload says that
# everything now goes out in clear text
cp "$WORK/erin/rpe2e/keyring.json" "$WORK/erin-keyring.json"
printf 'not json' > "$WORK/erin/rpe2e/keyring.json"
say erin "/load e2e"
say erin "/unload e2e"
if wait_for "$WORK/erin/all.log" "the e2e module is unloaded while the keyring is lost" 10 &&
   grep -aqF "every message is now sent in clear text (/load e2e, then /e2e reset or /e2e import)" "$WORK/erin/all.log"; then
    pass "/unload e2e with the keyring lost: erin is told that everything now goes out in clear text"
else
    bad "/unload e2e with the keyring lost: erin is not told"
fi
rm -f "$WORK/erin/rpe2e/keyring.json".corrupt-*
cp "$WORK/erin-keyring.json" "$WORK/erin/rpe2e/keyring.json"
say erin "/load e2e"
say erin "/upgrade"
if wait_for "$WORK/erin/all.log" "/UPGRADE: the new process will not load the e2e module" 15 &&
   grep -aqF "will be sent in clear text until /load e2e" "$WORK/erin/all.log"; then
    pass "/upgrade with E2E on, e2e not in autoload_modules: erin is warned first"
else
    bad "/upgrade with E2E on, e2e not in autoload_modules: no warning"
fi

for f in "$WORK"/asan-* "$WORK"/ubsan-*; do
    [ -e "$f" ] || continue
    bad "sanitizer report: $(basename "$f")"
    sed -n '1,30p' "$f"
done

echo "result: $ok ok, $fail failed"
if [ "$fail" -ne 0 ]; then
    for nick in alice bob; do
        echo "--- $nick: last lines"
        tail -n 25 "$WORK/$nick/all.log" 2>/dev/null
    done
fi
[ "$fail" -eq 0 ]
