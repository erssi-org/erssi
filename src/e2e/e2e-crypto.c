/*
 e2e-crypto.c : RPE2E crypto primitives on top of OpenSSL

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The RPE2E v1.0 protocol (repartee, rpe2e.pl) is built on libsodium; this
 file provides the same primitives with OpenSSL only, byte for byte:

  - XChaCha20-Poly1305 is not in OpenSSL. It is HChaCha20(key, nonce[0..16])
    as the key of the IETF ChaCha20-Poly1305 with the nonce
    00000000 || nonce[16..24] (draft-irtf-cfrg-xchacha-03), so only
    HChaCha20 is implemented here; the AEAD itself is OpenSSL's.
  - The Ed25519 -> X25519 conversions (used by REKEY) have no OpenSSL API:
    the secret key is the clamped first half of SHA-512(seed), the public
    key the Montgomery u = (1 + y) / (1 - y) mod 2^255-19, computed with
    BIGNUM on the PUBLIC key (no secret is involved, so variable time is
    fine). A y that is no curve point is refused, as repartee
    (curve25519-dalek) does.
*/

#include "e2e-crypto.h"

#include <limits.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#define HKDF_SALT "RPE2E01-WRAP"
#define FP_DOMAIN "RPE2E01-FP:"

gboolean e2e_random_bytes(unsigned char *buf, size_t len)
{
	if (len > INT_MAX)
		return FALSE;
	return len == 0 || RAND_bytes(buf, (int) len) == 1;
}

void e2e_wipe(void *buf, size_t len)
{
	if (buf != NULL && len > 0)
		OPENSSL_cleanse(buf, len);
}

gboolean e2e_mem_equal(const void *a, const void *b, size_t len)
{
	return CRYPTO_memcmp(a, b, len) == 0;
}

gboolean e2e_sha256(const unsigned char *data, size_t len, unsigned char out[32])
{
	return EVP_Digest(data, len, out, NULL, EVP_sha256(), NULL) == 1;
}

gboolean e2e_sha512(const unsigned char *data, size_t len, unsigned char out[64])
{
	return EVP_Digest(data, len, out, NULL, EVP_sha512(), NULL) == 1;
}

/* RFC 5869 with HMAC-SHA256, exactly as rpe2e.pl writes it out (repartee
   uses the hkdf crate): PRK = HMAC(salt, IKM), T(i) = HMAC(PRK,
   T(i-1) || info || i). One-shot HMAC() is in OpenSSL 1.1.1 and 3.x. */
gboolean e2e_hkdf_sha256(const unsigned char *salt, size_t saltlen,
                         const unsigned char *ikm, size_t ikmlen,
                         const unsigned char *info, size_t infolen,
                         unsigned char *out, size_t outlen)
{
	unsigned char prk[32], block[32];
	unsigned char *buf;
	unsigned int mdlen;
	size_t done, buflen, prevlen;
	int counter;
	gboolean ok = FALSE;

	if (outlen > 255 * 32 || saltlen > INT_MAX || infolen > INT_MAX - 64)
		return FALSE;
	mdlen = sizeof(prk);
	if (HMAC(EVP_sha256(), salt, (int) saltlen, ikm, ikmlen, prk, &mdlen) == NULL)
		return FALSE;

	buf = g_malloc(32 + infolen + 1);
	prevlen = 0;
	done = 0;
	for (counter = 1; done < outlen; counter++) {
		size_t take;

		/* buf = T(i-1) || info || i; T(i-1) is still in buf[0..32) */
		if (infolen > 0)
			memcpy(buf + prevlen, info, infolen);
		buf[prevlen + infolen] = (unsigned char) counter;
		buflen = prevlen + infolen + 1;
		mdlen = sizeof(block);
		if (HMAC(EVP_sha256(), prk, sizeof(prk), buf, buflen, block, &mdlen) == NULL)
			goto out;
		take = MIN(outlen - done, sizeof(block));
		memcpy(out + done, block, take);
		done += take;
		memcpy(buf, block, sizeof(block));
		prevlen = sizeof(block);
	}
	ok = TRUE;
out:
	e2e_wipe(prk, sizeof(prk));
	e2e_wipe(block, sizeof(block));
	e2e_wipe(buf, 32 + infolen + 1);
	g_free(buf);
	return ok;
}

gboolean e2e_wrap_key(unsigned char out[32], const unsigned char shared[32],
                      const char *info, size_t infolen)
{
	return e2e_hkdf_sha256((const unsigned char *) HKDF_SALT, strlen(HKDF_SALT),
	                       shared, 32, (const unsigned char *) info, infolen,
	                       out, 32);
}

gboolean e2e_fingerprint(const unsigned char pk[32], unsigned char fp[16])
{
	unsigned char buf[sizeof(FP_DOMAIN) - 1 + 32], digest[32];

	memcpy(buf, FP_DOMAIN, sizeof(FP_DOMAIN) - 1);
	memcpy(buf + sizeof(FP_DOMAIN) - 1, pk, 32);
	if (!e2e_sha256(buf, sizeof(buf), digest))
		return FALSE;
	memcpy(fp, digest, 16);
	return TRUE;
}

/* ---- HChaCha20 / XChaCha20-Poly1305 ---- */

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define QUARTERROUND(a, b, c, d) G_STMT_START { \
	a += b; d ^= a; d = ROTL32(d, 16); \
	c += d; b ^= c; b = ROTL32(b, 12); \
	a += b; d ^= a; d = ROTL32(d, 8); \
	c += d; b ^= c; b = ROTL32(b, 7); \
} G_STMT_END

static guint32 load32_le(const unsigned char *p)
{
	return (guint32) p[0] | ((guint32) p[1] << 8) | ((guint32) p[2] << 16) |
	       ((guint32) p[3] << 24);
}

static void store32_le(unsigned char *p, guint32 v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

/* draft-irtf-cfrg-xchacha-03 2.2: the ChaCha20 block function without the
   final addition, keeping words 0-3 and 12-15. Only additions, rotations
   and xors on fixed indexes: constant time by construction. */
void e2e_hchacha20(unsigned char out[32], const unsigned char key[32],
                   const unsigned char nonce[16])
{
	guint32 x[16];
	int i;

	x[0] = 0x61707865;
	x[1] = 0x3320646e;
	x[2] = 0x79622d32;
	x[3] = 0x6b206574;
	for (i = 0; i < 8; i++)
		x[4 + i] = load32_le(key + 4 * i);
	for (i = 0; i < 4; i++)
		x[12 + i] = load32_le(nonce + 4 * i);

	for (i = 0; i < 10; i++) {
		QUARTERROUND(x[0], x[4], x[8], x[12]);
		QUARTERROUND(x[1], x[5], x[9], x[13]);
		QUARTERROUND(x[2], x[6], x[10], x[14]);
		QUARTERROUND(x[3], x[7], x[11], x[15]);
		QUARTERROUND(x[0], x[5], x[10], x[15]);
		QUARTERROUND(x[1], x[6], x[11], x[12]);
		QUARTERROUND(x[2], x[7], x[8], x[13]);
		QUARTERROUND(x[3], x[4], x[9], x[14]);
	}

	for (i = 0; i < 4; i++) {
		store32_le(out + 4 * i, x[i]);
		store32_le(out + 16 + 4 * i, x[12 + i]);
	}
	e2e_wipe(x, sizeof(x));
}

/* subkey and 96-bit IETF nonce for one XChaCha20-Poly1305 operation */
static void xchacha_setup(unsigned char subkey[32], unsigned char iv[12],
                          const unsigned char nonce[24], const unsigned char key[32])
{
	e2e_hchacha20(subkey, key, nonce);
	memset(iv, 0, 4);
	memcpy(iv + 4, nonce + 16, 8);
}

gboolean e2e_xchacha_encrypt(unsigned char *out, const unsigned char *pt, size_t len,
                             const unsigned char *aad, size_t aadlen,
                             const unsigned char nonce[24], const unsigned char key[32])
{
	unsigned char subkey[32], iv[12];
	EVP_CIPHER_CTX *ctx;
	int outl, finl;
	gboolean ok = FALSE;

	if (len > INT_MAX - 16 || aadlen > INT_MAX)
		return FALSE;
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return FALSE;
	xchacha_setup(subkey, iv, nonce, key);

	if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1 ||
	    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, sizeof(iv), NULL) != 1 ||
	    EVP_EncryptInit_ex(ctx, NULL, NULL, subkey, iv) != 1)
		goto out;
	if (aadlen > 0 && EVP_EncryptUpdate(ctx, NULL, &outl, aad, (int) aadlen) != 1)
		goto out;
	outl = 0;
	if (len > 0 && EVP_EncryptUpdate(ctx, out, &outl, pt, (int) len) != 1)
		goto out;
	if (EVP_EncryptFinal_ex(ctx, out + outl, &finl) != 1 || (size_t) (outl + finl) != len)
		goto out;
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, E2E_TAG_LEN, out + len) != 1)
		goto out;
	ok = TRUE;
out:
	EVP_CIPHER_CTX_free(ctx);
	e2e_wipe(subkey, sizeof(subkey));
	return ok;
}

gboolean e2e_xchacha_decrypt(unsigned char *out, const unsigned char *in, size_t len,
                             const unsigned char *aad, size_t aadlen,
                             const unsigned char nonce[24], const unsigned char key[32])
{
	unsigned char subkey[32], iv[12], tag[E2E_TAG_LEN];
	EVP_CIPHER_CTX *ctx;
	size_t ctlen;
	int outl, finl;
	gboolean ok = FALSE;

	if (len < E2E_TAG_LEN || len > INT_MAX || aadlen > INT_MAX)
		return FALSE;
	ctlen = len - E2E_TAG_LEN;
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return FALSE;
	xchacha_setup(subkey, iv, nonce, key);
	memcpy(tag, in + ctlen, sizeof(tag));

	if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1 ||
	    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, sizeof(iv), NULL) != 1 ||
	    EVP_DecryptInit_ex(ctx, NULL, NULL, subkey, iv) != 1)
		goto out;
	if (aadlen > 0 && EVP_DecryptUpdate(ctx, NULL, &outl, aad, (int) aadlen) != 1)
		goto out;
	outl = 0;
	if (ctlen > 0 && EVP_DecryptUpdate(ctx, out, &outl, in, (int) ctlen) != 1)
		goto out;
	/* OpenSSL compares the tag in constant time in the final call */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, sizeof(tag), tag) != 1)
		goto out;
	if (EVP_DecryptFinal_ex(ctx, out + outl, &finl) != 1 || (size_t) (outl + finl) != ctlen)
		goto out;
	ok = TRUE;
out:
	EVP_CIPHER_CTX_free(ctx);
	e2e_wipe(subkey, sizeof(subkey));
	if (!ok)
		e2e_wipe(out, ctlen);
	return ok;
}

/* ---- Ed25519 ---- */

gboolean e2e_ed25519_seed_keypair(const unsigned char seed[32], unsigned char pk[32],
                                  unsigned char sk[64])
{
	EVP_PKEY *key;
	size_t len = 32;
	gboolean ok;

	key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
	if (key == NULL)
		return FALSE;
	ok = EVP_PKEY_get_raw_public_key(key, pk, &len) == 1 && len == 32;
	EVP_PKEY_free(key);
	if (!ok)
		return FALSE;
	/* libsodium's secret key layout, which keyring.json stores */
	memmove(sk, seed, 32);
	memcpy(sk + 32, pk, 32);
	return TRUE;
}

gboolean e2e_ed25519_keypair(unsigned char pk[32], unsigned char sk[64])
{
	unsigned char seed[32];
	gboolean ok;

	ok = e2e_random_bytes(seed, sizeof(seed)) && e2e_ed25519_seed_keypair(seed, pk, sk);
	e2e_wipe(seed, sizeof(seed));
	return ok;
}

gboolean e2e_ed25519_sign(unsigned char sig[64], const unsigned char *msg, size_t len,
                          const unsigned char sk[64])
{
	unsigned char pk[32];
	EVP_PKEY *key;
	EVP_MD_CTX *md;
	size_t siglen = 64;
	gboolean ok = FALSE;

	key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, sk, 32);
	if (key == NULL)
		return FALSE;
	/* libsodium signs with the public half stored in sk[32..64]; OpenSSL
	   derives it from the seed. A keyring whose halves disagree would
	   produce signatures nobody can verify - refuse instead. */
	siglen = sizeof(pk);
	if (EVP_PKEY_get_raw_public_key(key, pk, &siglen) != 1 || siglen != 32 ||
	    !e2e_mem_equal(pk, sk + 32, 32)) {
		EVP_PKEY_free(key);
		return FALSE;
	}
	siglen = 64;
	md = EVP_MD_CTX_new();
	if (md != NULL &&
	    EVP_DigestSignInit(md, NULL, NULL, NULL, key) == 1 &&
	    EVP_DigestSign(md, sig, &siglen, len > 0 ? msg : (const unsigned char *) "", len) == 1 &&
	    siglen == 64)
		ok = TRUE;
	EVP_MD_CTX_free(md);
	EVP_PKEY_free(key);
	return ok;
}

gboolean e2e_ed25519_verify(const unsigned char sig[64], const unsigned char *msg,
                            size_t len, const unsigned char pk[32])
{
	EVP_PKEY *key;
	EVP_MD_CTX *md;
	gboolean ok = FALSE;

	key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pk, 32);
	if (key == NULL)
		return FALSE;
	md = EVP_MD_CTX_new();
	if (md != NULL &&
	    EVP_DigestVerifyInit(md, NULL, NULL, NULL, key) == 1 &&
	    EVP_DigestVerify(md, sig, 64, len > 0 ? msg : (const unsigned char *) "", len) == 1)
		ok = TRUE;
	EVP_MD_CTX_free(md);
	EVP_PKEY_free(key);
	return ok;
}

/* ---- X25519 ---- */

gboolean e2e_x25519_public(unsigned char pk[32], const unsigned char sk[32])
{
	EVP_PKEY *key;
	size_t len = 32;
	gboolean ok;

	key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, sk, 32);
	if (key == NULL)
		return FALSE;
	ok = EVP_PKEY_get_raw_public_key(key, pk, &len) == 1 && len == 32;
	EVP_PKEY_free(key);
	return ok;
}

gboolean e2e_x25519_keypair(unsigned char sk[32], unsigned char pk[32])
{
	if (!e2e_random_bytes(sk, 32))
		return FALSE;
	/* clamped before it is stored, like rpe2e.pl's generate_x25519_keypair
	   (X25519 clamps again anyway) */
	sk[0] &= 248;
	sk[31] &= 127;
	sk[31] |= 64;
	return e2e_x25519_public(pk, sk);
}

gboolean e2e_x25519(unsigned char shared[32], const unsigned char sk[32],
                    const unsigned char pk[32])
{
	EVP_PKEY *priv, *pub;
	EVP_PKEY_CTX *ctx = NULL;
	size_t len = 32;
	unsigned char acc = 0;
	gboolean ok = FALSE;
	int i;

	priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, sk, 32);
	pub = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, pk, 32);
	if (priv == NULL || pub == NULL)
		goto out;
	ctx = EVP_PKEY_CTX_new(priv, NULL);
	if (ctx == NULL || EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pub) != 1 ||
	    EVP_PKEY_derive(ctx, shared, &len) != 1 || len != 32)
		goto out;
	/* libsodium's crypto_scalarmult refuses an all-zero result; so does
	   OpenSSL, checked again here without branching on the bytes */
	for (i = 0; i < 32; i++)
		acc |= shared[i];
	ok = acc != 0;
out:
	if (!ok)
		e2e_wipe(shared, 32);
	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(priv);
	EVP_PKEY_free(pub);
	return ok;
}

/* ---- Ed25519 -> X25519 ---- */

gboolean e2e_ed25519_sk_to_x25519(unsigned char out[32], const unsigned char sk[64])
{
	unsigned char h[64];

	if (!e2e_sha512(sk, 32, h))
		return FALSE;
	memcpy(out, h, 32);
	out[0] &= 248;
	out[31] &= 127;
	out[31] |= 64;
	e2e_wipe(h, sizeof(h));
	return TRUE;
}

gboolean e2e_ed25519_pk_to_x25519(unsigned char out[32], const unsigned char pk[32])
{
	unsigned char ybytes[32];
	BN_CTX *bc;
	BIGNUM *p, *y, *y2, *one, *d, *t, *u, *v, *w, *e;
	gboolean ok = FALSE;

	bc = BN_CTX_new();
	if (bc == NULL)
		return FALSE;
	BN_CTX_start(bc);
	p = BN_CTX_get(bc);
	y = BN_CTX_get(bc);
	y2 = BN_CTX_get(bc);
	one = BN_CTX_get(bc);
	d = BN_CTX_get(bc);
	t = BN_CTX_get(bc);
	u = BN_CTX_get(bc);
	v = BN_CTX_get(bc);
	w = BN_CTX_get(bc);
	e = BN_CTX_get(bc);
	if (e == NULL)
		goto out;

	/* p = 2^255 - 19 */
	BN_zero(p);
	if (!BN_set_bit(p, 255) || !BN_sub_word(p, 19) || !BN_one(one))
		goto out;

	/* y: little endian, the sign bit of x cleared, reduced modulo p
	   (curve25519-dalek's FieldElement::from_bytes) */
	memcpy(ybytes, pk, 32);
	ybytes[31] &= 0x7f;
	if (BN_lebin2bn(ybytes, 32, y) == NULL || !BN_nnmod(y, y, p, bc))
		goto out;

	/* d = -121665 / 121666 */
	if (!BN_set_word(t, 121666) || BN_mod_inverse(t, t, p, bc) == NULL ||
	    !BN_set_word(d, 121665) || !BN_mod_mul(d, d, t, p, bc) ||
	    !BN_mod_sub(d, p, d, p, bc))
		goto out;

	/* a point exists for y when x^2 = (y^2 - 1) / (d y^2 + 1) has a root:
	   w = u / v is zero or a quadratic residue (Euler's criterion) */
	if (!BN_mod_sqr(y2, y, p, bc) ||
	    !BN_mod_sub(u, y2, one, p, bc) ||
	    !BN_mod_mul(v, d, y2, p, bc) || !BN_mod_add(v, v, one, p, bc))
		goto out;
	if (BN_is_zero(v) || BN_mod_inverse(t, v, p, bc) == NULL ||
	    !BN_mod_mul(w, u, t, p, bc))
		goto out;
	if (!BN_is_zero(w)) {
		if (!BN_sub(e, p, one) || !BN_rshift1(e, e) ||
		    !BN_mod_exp(t, w, e, p, bc) || !BN_is_one(t))
			goto out;
	}

	/* u = (1 + y) / (1 - y); 1 - y = 0 gives 0, as dalek's invert(0) */
	if (!BN_mod_add(u, one, y, p, bc) || !BN_mod_sub(v, one, y, p, bc))
		goto out;
	if (BN_is_zero(v)) {
		BN_zero(u);
	} else if (BN_mod_inverse(t, v, p, bc) == NULL || !BN_mod_mul(u, u, t, p, bc)) {
		goto out;
	}
	if (BN_bn2lebinpad(u, out, 32) != 32)
		goto out;
	ok = TRUE;
out:
	BN_CTX_end(bc);
	BN_CTX_free(bc);
	return ok;
}
