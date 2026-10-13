/*
 e2e-legacy-fixture.h : a keyring as rpe2e.pl 0.2.0 leaves it - unscoped
 rows only, no network

 "#test" has one row in each of the nine places a context is stored
 (channels, outgoing, incoming, accepted, pending, outgoing_recipients,
 pending_inbound, seen_rekeys, pending_trust_change). "#other", "#test2"
 and "#te" are there to show that only "#test" moves. bob's session key
 on #test is 0x11 x 32, the key of rpe2e-vectors.h.
*/

#ifndef E2E_LEGACY_FIXTURE_H
#define E2E_LEGACY_FIXTURE_H

#include <irssi/src/e2e/e2e-json.h>

#define LEGACY_OUT_SK  "ZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmY="	/* 0x66 x 32 */
#define LEGACY_BOB_SK  "ERERERERERERERERERERERERERERERERERERERERERE="	/* 0x11 x 32 */
#define LEGACY_BOB_FP  "b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0"
#define LEGACY_ROWS_TEST 9

static const char legacy_fixture_json[] =
	"{"
	"\"channels\":{"
	  "\"#test\":{\"enabled\":1,\"mode\":\"normal\"},"
	  "\"#other\":{\"enabled\":1,\"mode\":\"normal\"},"
	  "\"#test2\":{\"enabled\":1,\"mode\":\"quiet\"}},"
	"\"outgoing\":{"
	  "\"#test\":{\"sk\":\"" LEGACY_OUT_SK "\",\"created_at\":1791564000,\"pending_rotation\":0},"
	  "\"#other\":{\"sk\":\"d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3c=\",\"created_at\":1791564000,\"pending_rotation\":0}},"
	"\"incoming\":{"
	  "\"bob@127.0.0.1|#test\":{\"fp\":\"" LEGACY_BOB_FP "\",\"sk\":\"" LEGACY_BOB_SK "\",\"status\":\"trusted\",\"created_at\":1791564000},"
	  "\"bob@127.0.0.1|#other\":{\"fp\":\"" LEGACY_BOB_FP "\",\"sk\":\"" LEGACY_BOB_SK "\",\"status\":\"trusted\",\"created_at\":1791564000},"
	  "\"bob@127.0.0.1|#test2\":{\"fp\":\"" LEGACY_BOB_FP "\",\"sk\":\"" LEGACY_BOB_SK "\",\"status\":\"trusted\",\"created_at\":1791564000},"
	  "\"bob@127.0.0.1|#te\":{\"fp\":\"" LEGACY_BOB_FP "\",\"sk\":\"" LEGACY_BOB_SK "\",\"status\":\"trusted\",\"created_at\":1791564000}},"
	"\"accepted\":{"
	  "\"bob@127.0.0.1|#test\":{\"fp\":\"" LEGACY_BOB_FP "\",\"accepted_at\":1791564000,\"by\":\"accept\"},"
	  "\"bob@127.0.0.1|#other\":{\"fp\":\"" LEGACY_BOB_FP "\",\"accepted_at\":1791564000,\"by\":\"accept\"}},"
	"\"pending\":{"
	  "\"#test|carol@127.0.0.1\":{\"eph_sk\":\"VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVU=\",\"handle\":\"carol@127.0.0.1\",\"channel\":\"#test\",\"created_at\":1791564000},"
	  "\"#te|st|carol@127.0.0.1\":{\"eph_sk\":\"VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVU=\",\"handle\":\"carol@127.0.0.1\",\"channel\":\"#te|st\",\"created_at\":1791564000}},"
	"\"outgoing_recipients\":{"
	  "\"#test|bob@127.0.0.1\":{\"channel\":\"#test\",\"handle\":\"bob@127.0.0.1\",\"fingerprint\":\"" LEGACY_BOB_FP "\",\"first_sent_at\":1791564000},"
	  "\"#other|bob@127.0.0.1\":{\"channel\":\"#other\",\"handle\":\"bob@127.0.0.1\",\"fingerprint\":\"" LEGACY_BOB_FP "\",\"first_sent_at\":1791564000}},"
	"\"pending_inbound\":{"
	  "\"dave@127.0.0.1|#test\":{\"handle\":\"dave@127.0.0.1\",\"channel\":\"#test\",\"sender_handle\":\"dave@127.0.0.1\",\"sender_nick\":\"dave\",\"received_at\":1791564000}},"
	"\"seen_rekeys\":{"
	  "\"" LEGACY_BOB_FP "|#test\":{\"mZmZmZmZmZmZmZmZmZmZmQ==\":1791564000},"
	  "\"" LEGACY_BOB_FP "|#other\":{\"mZmZmZmZmZmZmZmZmZmZmQ==\":1791564000}},"
	"\"pending_trust_change\":["
	  "{\"handle\":\"eve@127.0.0.1\",\"channel\":\"#test\",\"change\":\"handle_changed\",\"new_pubkey\":null,\"old_fp\":\"00\",\"new_fp\":\"00\",\"recorded_at\":1791564000},"
	  "{\"handle\":\"eve@127.0.0.1\",\"channel\":\"#other\",\"change\":\"handle_changed\",\"new_pubkey\":null,\"old_fp\":\"00\",\"new_fp\":\"00\",\"recorded_at\":1791564000}],"
	"\"autotrust\":[],"
	"\"peers\":{}"
	"}";

/* the fixture's members into kr (its identity stays) */
static inline void legacy_fixture_fill(E2E_JSON *kr)
{
	const char *error = NULL;
	E2E_JSON *doc = e2e_json_parse(legacy_fixture_json, sizeof(legacy_fixture_json) - 1, &error);
	GPtrArray *keys;
	guint i;

	g_assert_nonnull(doc);
	keys = e2e_json_keys(doc);
	for (i = 0; i < keys->len; i++) {
		const char *k = g_ptr_array_index(keys, i);

		e2e_json_set(kr, k, e2e_json_copy(e2e_json_get(doc, k)));
	}
	g_ptr_array_unref(keys);
	e2e_json_free(doc);
}

#endif
