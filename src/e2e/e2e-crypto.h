#ifndef IRSSI_E2E_CRYPTO_H
#define IRSSI_E2E_CRYPTO_H

/*
 RPE2E crypto primitives, all on top of OpenSSL (>= 1.1.1):

  - Ed25519 identities (keys stored as libsodium does: 64-byte secret key
    = 32-byte seed || 32-byte public key),
  - X25519 ephemeral keys and ECDH,
  - HKDF-SHA256 for the key-wrap keys,
  - XChaCha20-Poly1305 (HChaCha20 subkey + IETF ChaCha20-Poly1305),
  - the Ed25519 -> X25519 conversions libsodium and repartee use.

 Every function returns FALSE on failure; output buffers are then left in
 an unspecified state and must not be used.
*/

#include <glib.h>
#include <stddef.h>

#define E2E_KEY_LEN      32
#define E2E_XNONCE_LEN   24
#define E2E_TAG_LEN      16
#define E2E_ED_PK_LEN    32
#define E2E_ED_SK_LEN    64
#define E2E_ED_SIG_LEN   64
#define E2E_X_LEN        32
#define E2E_FP_LEN       16

gboolean e2e_random_bytes(unsigned char *buf, size_t len);
/* OPENSSL_cleanse */
void e2e_wipe(void *buf, size_t len);
/* constant time, for secrets */
gboolean e2e_mem_equal(const void *a, const void *b, size_t len);

gboolean e2e_sha256(const unsigned char *data, size_t len, unsigned char out[32]);
gboolean e2e_sha512(const unsigned char *data, size_t len, unsigned char out[64]);
gboolean e2e_hkdf_sha256(const unsigned char *salt, size_t saltlen,
                         const unsigned char *ikm, size_t ikmlen,
                         const unsigned char *info, size_t infolen,
                         unsigned char *out, size_t outlen);
/* HKDF-SHA256 with the RPE2E salt "RPE2E01-WRAP" and a 32-byte output */
gboolean e2e_wrap_key(unsigned char out[32], const unsigned char shared[32],
                      const char *info, size_t infolen);
/* first 16 bytes of SHA-256("RPE2E01-FP:" || pk) */
gboolean e2e_fingerprint(const unsigned char pk[32], unsigned char fp[16]);

void e2e_hchacha20(unsigned char out[32], const unsigned char key[32],
                   const unsigned char nonce[16]);
/* out holds len + 16 bytes (ciphertext || tag) */
gboolean e2e_xchacha_encrypt(unsigned char *out, const unsigned char *pt, size_t len,
                             const unsigned char *aad, size_t aadlen,
                             const unsigned char nonce[24], const unsigned char key[32]);
/* in holds len >= 16 bytes, out len - 16; FALSE when the tag does not match */
gboolean e2e_xchacha_decrypt(unsigned char *out, const unsigned char *in, size_t len,
                             const unsigned char *aad, size_t aadlen,
                             const unsigned char nonce[24], const unsigned char key[32]);

gboolean e2e_ed25519_keypair(unsigned char pk[32], unsigned char sk[64]);
gboolean e2e_ed25519_seed_keypair(const unsigned char seed[32], unsigned char pk[32],
                                  unsigned char sk[64]);
gboolean e2e_ed25519_sign(unsigned char sig[64], const unsigned char *msg, size_t len,
                          const unsigned char sk[64]);
gboolean e2e_ed25519_verify(const unsigned char sig[64], const unsigned char *msg,
                            size_t len, const unsigned char pk[32]);

/* a random, clamped X25519 secret key and its public key */
gboolean e2e_x25519_keypair(unsigned char sk[32], unsigned char pk[32]);
gboolean e2e_x25519_public(unsigned char pk[32], const unsigned char sk[32]);
/* FALSE also for an all-zero shared secret (peer key of small order) */
gboolean e2e_x25519(unsigned char shared[32], const unsigned char sk[32],
                    const unsigned char pk[32]);

/* Montgomery u = (1 + y) / (1 - y) of an Ed25519 public key; FALSE when the
   encoding is no curve point */
gboolean e2e_ed25519_pk_to_x25519(unsigned char out[32], const unsigned char pk[32]);
/* clamped first half of SHA-512(seed) */
gboolean e2e_ed25519_sk_to_x25519(unsigned char out[32], const unsigned char sk[64]);

#endif
