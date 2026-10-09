/*
 crypto.c : fuzz the AES-256-GCM layer of fe-web

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The input is the payload of a binary frame: fe_web_crypto_decrypt() must
 refuse it (short, wrong tag) without reading or writing out of bounds.
 Then the input is encrypted and decrypted again, which must give it back.
*/

#include <irssi/src/fe-fuzz/fe-web/fe-web-fuzz.h>
#include <irssi/src/fe-web/fe-web-crypto.h>

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	fe_web_fuzz_init(FALSE);
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const unsigned char *key = fe_web_crypto_get_key();
	unsigned char *in, *out, *sealed;
	int out_len = -1, sealed_len = -1;

	if (size > FE_WEB_MAX_FRAME)
		return 0;

	/* as fe-web-server.c calls it: output buffer of the input's size */
	in = g_malloc(size > 0 ? size : 1);
	memcpy(in, data, size);
	out = g_malloc(size + 1);
	if (fe_web_crypto_decrypt(in, size, key, out, &out_len)) {
		if (out_len < 0 ||
		    (gsize) out_len + FE_WEB_CRYPTO_IV_SIZE + FE_WEB_CRYPTO_TAG_SIZE != size)
			abort();
	}
	g_free(out);

	/* round trip, buffer sized as fe-web-utils.c sizes it */
	sealed = g_malloc(size + FE_WEB_CRYPTO_IV_SIZE + FE_WEB_CRYPTO_TAG_SIZE);
	if (!fe_web_crypto_encrypt(in, size, key, sealed, &sealed_len))
		abort();
	if (sealed_len < 0 ||
	    (gsize) sealed_len != size + FE_WEB_CRYPTO_IV_SIZE + FE_WEB_CRYPTO_TAG_SIZE)
		abort();
	out = g_malloc(sealed_len + 1);
	if (!fe_web_crypto_decrypt(sealed, sealed_len, key, out, &out_len))
		abort();
	if (out_len < 0 || (gsize) out_len != size || memcmp(out, in, size) != 0)
		abort();

	/* one flipped bit anywhere must fail the tag check */
	sealed[(size * 7) % sealed_len] ^= 0x01;
	if (fe_web_crypto_decrypt(sealed, sealed_len, key, out, &out_len))
		abort();

	g_free(out);
	g_free(sealed);
	g_free(in);
	return 0;
}
