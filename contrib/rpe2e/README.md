# Moving an RPE2E identity from repartee

erssi has end-to-end encryption built in: the `e2e` module (`src/e2e`,
`/help e2e`), wire-compatible with RPE2E v1.0 of
[repartee](https://github.com/outragedevs/repartee) and its irssi and
WeeChat scripts. It keeps its keys in `~/.erssi/rpe2e/keyring.json`, the
same file and format as repartee's `rpe2e.pl` script, so a user of the
script switches to the module without new keys: unload the script
(`/script unload rpe2e`, remove it from `scripts/autorun`); while the script
is loaded the module stays inactive.

This directory holds `rpe2e-from-repartee`, which moves an identity from
repartee itself into that keyring. The module is a port of the MIT-licensed
`rpe2e.pl`; the licence text is in [LICENSE](LICENSE).

## Moving keys from repartee

So that peers do not see a “new key”, erssi can take over the repartee
identity:

```sh
rpe2e-from-repartee --dry-run          # what would be moved
# in erssi first: /unload e2e  (nothing saves while the file is replaced)
rpe2e-from-repartee --channels-off --force   # write (the module created a keyring when it was first loaded)
# then in erssi: /load e2e
```

The tool opens the repartee database read-only, decrypts the stored secrets
(`REPARTEE_KEYRING_KEY` from `~/.repartee/.env`) and checks them
cryptographically: it derives the public key from the seed and compares
fingerprints. When a channel appears under several names (`#chan` and
`IRCnet\x1f#chan`) the newest session wins, with a warning when the
variants come from different networks. An existing keyring is replaced
only with `--force` (a backup is kept). Secrets are never printed. Verified
on a real repartee keyring: the moved keys decrypted every `+RPE2E01`
message stored in the channel log.

If a peer knows you under a different `ident@host` (e.g. a new host), their
client warns about the change; the key is the same, so after comparing
fingerprints they run `/e2e reverify <your nick>`.

The module gives your key only to people you accepted (`/e2e accept`, an
autotrust rule or auto-accept mode) and keeps that decision in the keyring
(`"accepted"`). repartee and `rpe2e.pl` have no such record, so after the
move a peer that asks for your key again needs one `/e2e accept`, and a
peer not accepted yet gets no REKEY when your key rotates (it asks again
with its next undecryptable message).

## Tests

- `python3 test_rpe2e_from_repartee.py` — migration on a synthetic repartee
  database (real libsodium keys, AES-256-GCM secrets), including rejection
  of a wrong key and inconsistent data.
- The module itself is tested by `meson test` (`tests/e2e`, `src/fe-fuzz/e2e`);
  `tests/e2e/live-interop.sh` runs it live against repartee's `rpe2e.pl`
  when `RPE2E_SCRIPT` points to a copy of the script (0.2.0 needs two
  changes to run in erssi; the top of `live-interop.sh` says which).
