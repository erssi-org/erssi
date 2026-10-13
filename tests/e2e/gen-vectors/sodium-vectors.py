#!/usr/bin/env python3
# sodium-vectors.py - the cross-check vectors of test-e2e-crypto.c, computed
# with libsodium (the runtime library is enough: libsodium.so.23, through
# ctypes) and Python's hmac. Prints them; test-e2e-crypto.c holds a copy.
import ctypes, hmac, hashlib
s = ctypes.CDLL("libsodium.so.23"); assert s.sodium_init() >= 0
ull = ctypes.c_ulonglong

def xchacha_enc(key, nonce, aad, pt):
    out = ctypes.create_string_buffer(len(pt) + 16); n = ull(0)
    rc = s.crypto_aead_xchacha20poly1305_ietf_encrypt(out, ctypes.byref(n), pt, ull(len(pt)), aad, ull(len(aad)), None, nonce, key)
    assert rc == 0
    return out.raw[:n.value]

def pk2x(pk):
    out = ctypes.create_string_buffer(32)
    rc = s.crypto_sign_ed25519_pk_to_curve25519(out, pk)
    return out.raw if rc == 0 else None

def sk2x(sk64):
    out = ctypes.create_string_buffer(32)
    assert s.crypto_sign_ed25519_sk_to_curve25519(out, sk64) == 0
    return out.raw

def seed_kp(seed):
    pk = ctypes.create_string_buffer(32); sk = ctypes.create_string_buffer(64)
    assert s.crypto_sign_seed_keypair(pk, sk, seed) == 0
    return pk.raw, sk.raw

def sign(sk64, msg):
    sig = ctypes.create_string_buffer(64)
    assert s.crypto_sign_detached(sig, None, msg, ull(len(msg)), sk64) == 0
    return sig.raw

def scalarmult_base(sk):
    out = ctypes.create_string_buffer(32)
    assert s.crypto_scalarmult_base(out, sk) == 0
    return out.raw

def scalarmult(sk, pk):
    out = ctypes.create_string_buffer(32)
    rc = s.crypto_scalarmult(out, sk, pk)
    return out.raw if rc == 0 else None

def hkdf(salt, ikm, info, n):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out = b""; prev = b""; c = 1
    while len(out) < n:
        prev = hmac.new(prk, prev + info + bytes([c]), hashlib.sha256).digest(); out += prev; c += 1
    return out[:n]

# draft-irtf-cfrg-xchacha-03 A.3.1
key = bytes(range(0x80, 0xa0)); nonce = bytes(range(0x40, 0x58))
aad = bytes.fromhex("50515253c0c1c2c3c4c5c6c7")
pt = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
ct = xchacha_enc(key, nonce, aad, pt)
print("xchacha A.3.1 ct+tag:", ct.hex())
# empty plaintext, empty aad
print("xchacha empty:", xchacha_enc(bytes(32), bytes(24), b"", b"").hex())

# ed25519 -> x25519 for RFC 8032 7.1 seeds and some derived ones
for seed_hex in ["9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
                 "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
                 "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
                 bytes(range(32)).hex(), ("ff"*32)]:
    seed = bytes.fromhex(seed_hex); pk, sk = seed_kp(seed)
    print("seed", seed_hex, "pk", pk.hex(), "xpk", pk2x(pk).hex(), "xsk", sk2x(sk).hex(),
          "check", scalarmult_base(sk2x(sk)).hex() == pk2x(pk).hex(),
          "sig(abc)", sign(sk, b"abc").hex())
# invalid points
for bad in ["02" + "00"*31, "ff"*31 + "7f"]:
    print("bad", bad, pk2x(bytes.fromhex(bad)))
print("hkdf", hkdf(b"RPE2E01-WRAP", bytes(range(32)), b"RPE2E01-WRAP:#test", 32).hex())
print("hkdf-zazolc", hkdf(b"RPE2E01-WRAP", bytes(range(1,33)), "RPE2E01-REKEY:#żaba".encode(), 32).hex())
print("fp", hashlib.sha256(b"RPE2E01-FP:" + bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")).hexdigest()[:32])
