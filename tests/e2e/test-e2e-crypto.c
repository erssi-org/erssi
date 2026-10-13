/*
 test-e2e-crypto.c : the RPE2E crypto primitives against fixed vectors

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 Sources of the vectors:
  - HChaCha20 and XChaCha20-Poly1305: draft-irtf-cfrg-xchacha-03, sections
    2.2.1 and A.3.1 (the same values come out of libsodium 1.0.18,
    crypto_core_hchacha20 and crypto_aead_xchacha20poly1305_ietf_encrypt);
  - Ed25519 -> X25519 and Ed25519 signatures: libsodium,
    crypto_sign_ed25519_{pk,sk}_to_curve25519 and crypto_sign_detached,
    for the RFC 8032 section 7.1 seeds and a few more;
  - HKDF-SHA256: Python's hmac module (RFC 5869 by hand);
  - the Montgomery u of the small-order point y=3: computed with Python
    integers as (1+y)/(1-y) mod 2^255-19 (what repartee's curve25519-dalek
    to_montgomery() returns; libsodium refuses that point).
*/

#include <glib.h>
#include <string.h>

#include <irssi/src/e2e/e2e-crypto.h>

static void unhex(const char *hex, unsigned char *out, gsize len)
{
	gsize i;

	g_assert_cmpuint(strlen(hex), ==, len * 2);
	for (i = 0; i < len; i++)
		out[i] = (g_ascii_xdigit_value(hex[2 * i]) << 4) |
		         g_ascii_xdigit_value(hex[2 * i + 1]);
}

static char *tohex(const unsigned char *data, gsize len)
{
	GString *s = g_string_new(NULL);
	gsize i;

	for (i = 0; i < len; i++)
		g_string_append_printf(s, "%02x", data[i]);
	return g_string_free(s, FALSE);
}

#define assert_hex(data, len, hex) G_STMT_START { \
	char *got_ = tohex((data), (len)); \
	g_assert_cmpstr(got_, ==, (hex)); \
	g_free(got_); \
} G_STMT_END

static void test_hchacha20(void)
{
	unsigned char key[32], nonce[16], out[32];
	int i;

	for (i = 0; i < 32; i++)
		key[i] = i;
	unhex("000000090000004a0000000031415927", nonce, 16);
	e2e_hchacha20(out, key, nonce);
	assert_hex(out, 32, "82413b4227b27bfed30e42508a877d73a0f9e4d58a74a853c12ec41326d3ecdc");
}

static const char sunscreen[] =
	"Ladies and Gentlemen of the class of '99: If I could offer you only "
	"one tip for the future, sunscreen would be it.";
static const char sunscreen_ct[] =
	"bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb"
	"731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b452"
	"2f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff9"
	"21f9664c97637da9768812f615c68b13b52e"
	"c0875924c1c7987947deafd8780acf49";

static void test_xchacha_draft_vector(void)
{
	unsigned char key[32], nonce[24], aad[12], ct[sizeof(sunscreen) - 1 + 16];
	unsigned char pt[sizeof(sunscreen) - 1];
	gsize len = sizeof(sunscreen) - 1;
	int i;

	for (i = 0; i < 32; i++)
		key[i] = 0x80 + i;
	for (i = 0; i < 24; i++)
		nonce[i] = 0x40 + i;
	unhex("50515253c0c1c2c3c4c5c6c7", aad, sizeof(aad));

	g_assert_true(e2e_xchacha_encrypt(ct, (const unsigned char *) sunscreen, len,
	                                  aad, sizeof(aad), nonce, key));
	assert_hex(ct, sizeof(ct), sunscreen_ct);

	g_assert_true(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));
	g_assert_cmpmem(pt, len, sunscreen, len);

	/* any change of the tag, the ciphertext, the AAD or the nonce fails */
	ct[sizeof(ct) - 1] ^= 1;
	g_assert_false(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));
	ct[sizeof(ct) - 1] ^= 1;
	ct[0] ^= 0x80;
	g_assert_false(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));
	ct[0] ^= 0x80;
	aad[0] ^= 1;
	g_assert_false(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));
	aad[0] ^= 1;
	nonce[23] ^= 1;	/* the part that is not in the HChaCha20 subkey */
	g_assert_false(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));
	nonce[23] ^= 1;
	nonce[0] ^= 1;	/* the part that is */
	g_assert_false(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));
	nonce[0] ^= 1;
	g_assert_true(e2e_xchacha_decrypt(pt, ct, sizeof(ct), aad, sizeof(aad), nonce, key));

	/* shorter than a tag */
	g_assert_false(e2e_xchacha_decrypt(pt, ct, 15, aad, sizeof(aad), nonce, key));
}

static void test_xchacha_empty(void)
{
	unsigned char key[32] = { 0 }, nonce[24] = { 0 }, ct[16], pt[1];

	/* libsodium: empty message, empty AAD, all-zero key and nonce */
	g_assert_true(e2e_xchacha_encrypt(ct, NULL, 0, NULL, 0, nonce, key));
	assert_hex(ct, 16, "8f3b945a51906dc8600de9f8962d00e6");
	g_assert_true(e2e_xchacha_decrypt(pt, ct, 16, NULL, 0, nonce, key));
}

static const struct {
	const char *seed, *pk, *xpk, *xsk, *sig_abc;
} ed_vectors[] = {
	{ "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
	  "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
	  "d85e07ec22b0ad881537c2f44d662d1a143cf830c57aca4305d85c7a90f6b62e",
	  "307c83864f2833cb427a2ef1c00a013cfdff2768d980c0a3a520f006904de94f",
	  "80d724b01e7ca260f4cc7f8de7c95f73cfac615bab1f762b6435b6ec26c8cf6d"
	  "2c758dae2f87399a8eeda1cbcd2835ac5ba66d6ecaa3aba5e567a751053dc207" },
	{ "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
	  "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
	  "25c704c594b88afc00a76b69d1ed2b984d7e22550f3ed0802d04fbcd07d38d47",
	  "68bd9ed75882d52815a97585caf4790a7f6c6b3b7f821c5e259a24b02e502e51",
	  "a377ba06339de184aa1a88b883d8558b704b96f7e4b2ac3e7984fc297e9549bd"
	  "4ee1bcbb72553fc768400ec2395a702311e7aed969a713387b3377855a537701" },
	{ "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
	  "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
	  "cbb22fc9f790bd3eba9b84680c157ca4950a9894362601701f89c3c4d9fda23a",
	  "909a8b755ed902849023a55b15c23d11ba4d7f4ec5c2f51b1325a181991ea95c",
	  "34bf2f0eba20dfbff08e8218a18fbbf0cfd521616bbe5d781e96150cb1b48599"
	  "277944b1052bcb6d88d84d5fca176ecda8b32429557009ab357c7d536dce4b00" },
	{ "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
	  "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8",
	  "4701d08488451f545a409fb58ae3e58581ca40ac3f7f114698cd71deac73ca01",
	  "3894eea49c580aef816935762be049559d6d1440dede12e6a125f1841fff8e6f",
	  "cc46d62d3754f41754b27b6ea2cb2c272bafa7a5a1f6062bd060f414e50caaea"
	  "c2da66ad39cef4424a90236ea907b7d8057e3443dc5abfc9986967ee7213a407" },
	{ "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
	  "76a1592044a6e4f511265bca73a604d90b0529d1df602be30a19a9257660d1f5",
	  "d1fa3f01826bd8b78e057c086c7b22c7ad4358ca918099cd7b7e5d3acd7e285b",
	  "20cd6935864716a79d74dd5fabbd8964304051ca41a31c4659158ebb7c3d0b57",
	  "aec6ab6a9122aff0f7dcb9667ff613136894732b6e78c26f5b673101e267fe2e"
	  "2b65fa4d53dad478a1ada64d50fd1dfdb7d94920dc3e1a564a647b1cba356001" },
};

static void test_ed25519_vectors(void)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS(ed_vectors); i++) {
		unsigned char seed[32], pk[32], sk[64], x[32], xpub[32], sig[64];

		unhex(ed_vectors[i].seed, seed, 32);
		g_assert_true(e2e_ed25519_seed_keypair(seed, pk, sk));
		assert_hex(pk, 32, ed_vectors[i].pk);
		/* libsodium layout: seed || public key */
		g_assert_cmpmem(sk, 32, seed, 32);
		g_assert_cmpmem(sk + 32, 32, pk, 32);

		g_assert_true(e2e_ed25519_pk_to_x25519(x, pk));
		assert_hex(x, 32, ed_vectors[i].xpk);
		g_assert_true(e2e_ed25519_sk_to_x25519(x, sk));
		assert_hex(x, 32, ed_vectors[i].xsk);
		/* the two conversions describe the same X25519 key pair */
		g_assert_true(e2e_x25519_public(xpub, x));
		assert_hex(xpub, 32, ed_vectors[i].xpk);

		g_assert_true(e2e_ed25519_sign(sig, (const unsigned char *) "abc", 3, sk));
		assert_hex(sig, 64, ed_vectors[i].sig_abc);
		g_assert_true(e2e_ed25519_verify(sig, (const unsigned char *) "abc", 3, pk));
		g_assert_false(e2e_ed25519_verify(sig, (const unsigned char *) "abd", 3, pk));
		sig[10] ^= 1;
		g_assert_false(e2e_ed25519_verify(sig, (const unsigned char *) "abc", 3, pk));
	}
}

static void test_ed_to_x_points(void)
{
	unsigned char pk[32], x[32];

	/* y = 2 and y = 7 are not on the curve: rejected by libsodium and by
	   repartee (curve25519-dalek decompression) alike */
	memset(pk, 0, sizeof(pk));
	pk[0] = 2;
	g_assert_false(e2e_ed25519_pk_to_x25519(x, pk));
	pk[0] = 7;
	g_assert_false(e2e_ed25519_pk_to_x25519(x, pk));
	/* the sign bit does not make a point valid */
	pk[0] = 2;
	pk[31] = 0x80;
	g_assert_false(e2e_ed25519_pk_to_x25519(x, pk));

	/* y = 3 is on the curve (small order): repartee converts it, so does
	   the native code; -2 mod p */
	memset(pk, 0, sizeof(pk));
	pk[0] = 3;
	g_assert_true(e2e_ed25519_pk_to_x25519(x, pk));
	assert_hex(x, 32, "ebffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f");

	/* y = 1 (the neutral element): 1 - y = 0, dalek's inversion of zero
	   gives u = 0 */
	pk[0] = 1;
	g_assert_true(e2e_ed25519_pk_to_x25519(x, pk));
	assert_hex(x, 32, "0000000000000000000000000000000000000000000000000000000000000000");

	/* a non-canonical y (p + 2 = 2 mod p) is read modulo p, as dalek does */
	memset(pk, 0xff, sizeof(pk));
	pk[0] = 0xef;
	pk[31] = 0x7f;
	g_assert_false(e2e_ed25519_pk_to_x25519(x, pk));
}

static void test_x25519(void)
{
	unsigned char a[32], b[32], apub[32], bpub[32], s1[32], s2[32], zero[32] = { 0 };

	g_assert_true(e2e_x25519_keypair(a, apub));
	g_assert_true(e2e_x25519_keypair(b, bpub));
	/* stored clamped, the way rpe2e.pl generates it */
	g_assert_cmpint(a[0] & 7, ==, 0);
	g_assert_cmpint(a[31] & 0x80, ==, 0);
	g_assert_cmpint(a[31] & 0x40, ==, 0x40);
	g_assert_true(e2e_x25519(s1, a, bpub));
	g_assert_true(e2e_x25519(s2, b, apub));
	g_assert_cmpmem(s1, 32, s2, 32);
	/* a peer key of small order gives an all-zero secret: refused */
	g_assert_false(e2e_x25519(s1, a, zero));
}

static void test_hkdf(void)
{
	unsigned char ikm[32], out[32];
	int i;
	const char *info = "RPE2E01-WRAP:#test";
	const char *info2 = "RPE2E01-REKEY:#\xc5\xbc" "aba";

	for (i = 0; i < 32; i++)
		ikm[i] = i;
	g_assert_true(e2e_wrap_key(out, ikm, info, strlen(info)));
	assert_hex(out, 32, "fa1b3f3167252623f947688bdb9123383ea4f08c948a27fc2ec26cf9b19da3fe");
	for (i = 0; i < 32; i++)
		ikm[i] = i + 1;
	g_assert_true(e2e_wrap_key(out, ikm, info2, strlen(info2)));
	assert_hex(out, 32, "c4a314a321d94bdd2f5f5bdf0de278664547e7f73395629eebab584ab73903b5");
}

static void test_fingerprint(void)
{
	unsigned char pk[32], fp[16];

	unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk, 32);
	g_assert_true(e2e_fingerprint(pk, fp));
	assert_hex(fp, 16, "5de3937acf491f12fb0ec9f86f298cd8");
}

static void test_random_keypair(void)
{
	unsigned char pk[32], sk[64], pk2[32], sk2[64], sig[64];

	g_assert_true(e2e_ed25519_keypair(pk, sk));
	g_assert_true(e2e_ed25519_seed_keypair(sk, pk2, sk2));
	g_assert_cmpmem(pk, 32, pk2, 32);
	g_assert_true(e2e_ed25519_sign(sig, (const unsigned char *) "", 0, sk));
	g_assert_true(e2e_ed25519_verify(sig, (const unsigned char *) "", 0, pk));
	/* an inconsistent secret key (public half of someone else) is refused,
	   OpenSSL would otherwise sign with a key the peers do not know */
	sk[40] ^= 1;
	g_assert_false(e2e_ed25519_sign(sig, (const unsigned char *) "", 0, sk));
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/e2e/crypto/hchacha20", test_hchacha20);
	g_test_add_func("/e2e/crypto/xchacha-draft", test_xchacha_draft_vector);
	g_test_add_func("/e2e/crypto/xchacha-empty", test_xchacha_empty);
	g_test_add_func("/e2e/crypto/ed25519", test_ed25519_vectors);
	g_test_add_func("/e2e/crypto/ed-to-x-points", test_ed_to_x_points);
	g_test_add_func("/e2e/crypto/x25519", test_x25519);
	g_test_add_func("/e2e/crypto/hkdf", test_hkdf);
	g_test_add_func("/e2e/crypto/fingerprint", test_fingerprint);
	g_test_add_func("/e2e/crypto/random-keypair", test_random_keypair);

	return g_test_run();
}
