/*
 test-e2e-keyring.c : keyring.json - the format of rpe2e.pl and its safety

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The keyring is the file rpe2e.pl 0.2.0 keeps (<erssi dir>/rpe2e/
 keyring.json), so a user of the script switches without new keys. The
 module's stricter file rules are checked here: a file that cannot be read is never
 replaced, a corrupt one is kept aside, a failed write leaves the old file
 and no temporary file.
*/

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <irssi/src/e2e/e2e-crypto.h>
#include <irssi/src/e2e/e2e-json.h>
#include <irssi/src/e2e/e2e-keyring.h>
#include <irssi/src/e2e/e2e-proto.h>
#include <irssi/src/e2e/e2e-scope.h>
#include <irssi/src/e2e/e2e-wire.h>

static char *dir;
static char *path;

static void setup(void)
{
	dir = g_dir_make_tmp("e2e-keyring-XXXXXX", NULL);
	g_assert_nonnull(dir);
	path = g_build_filename(dir, "keyring.json", NULL);
}

static void remove_tree(void)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *name;

	while ((name = g_dir_read_name(d)) != NULL) {
		char *p = g_build_filename(dir, name, NULL);

		g_chmod(p, 0600);
		g_unlink(p);
		g_free(p);
	}
	g_dir_close(d);
	g_rmdir(dir);
	g_free(dir);
	g_free(path);
}

static char *slurp(const char *p)
{
	char *data = NULL;

	g_file_get_contents(p, &data, NULL, NULL);
	return data;
}

static void spew(const char *p, const char *data)
{
	g_assert_true(g_file_set_contents(p, data, -1, NULL));
}

static int count_files(const char *prefix)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *name;
	int n = 0;

	while ((name = g_dir_read_name(d)) != NULL)
		if (g_str_has_prefix(name, prefix))
			n++;
	g_dir_close(d);
	return n;
}

static char *first_file(const char *prefix)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *name;
	char *found = NULL;

	while ((name = g_dir_read_name(d)) != NULL)
		if (found == NULL && g_str_has_prefix(name, prefix))
			found = g_build_filename(dir, name, NULL);
	g_dir_close(d);
	return found;
}

/* ---- JSON ---- */

static E2E_JSON *parse(const char *text)
{
	const char *error = NULL;

	return e2e_json_parse(text, strlen(text), &error);
}

static void test_json_roundtrip(void)
{
	E2E_JSON *j;
	char *out;

	j = parse(" {\"b\": [1, -2.5e3, true, false, null], \"a\": \"x\\\"\\\\\\n\\t\\u0001/\","
	          " \"c\": {}} ");
	g_assert_nonnull(j);
	/* canonical: sorted keys, compact, numbers as written */
	out = e2e_json_encode(j, FALSE);
	g_assert_cmpstr(out, ==, "{\"a\":\"x\\\"\\\\\\n\\t\\u0001/\",\"b\":[1,-2.5e3,true,false,null],\"c\":{}}");
	g_free(out);
	/* JSON::PP->pretty: three spaces, " : " */
	out = e2e_json_encode(j, TRUE);
	g_assert_cmpstr(out, ==,
		"{\n"
		"   \"a\" : \"x\\\"\\\\\\n\\t\\u0001/\",\n"
		"   \"b\" : [\n"
		"      1,\n"
		"      -2.5e3,\n"
		"      true,\n"
		"      false,\n"
		"      null\n"
		"   ],\n"
		"   \"c\" : {}\n"
		"}\n");
	g_free(out);
	e2e_json_free(j);
}

static void test_json_rejects(void)
{
	static const char *const bad[] = {
		"", "{", "{\"a\":1,}", "{\"a\" 1}", "[1]", "\"x\"", "{} x", "{\"a\":01}",
		"{\"a\":1.}", "{\"a\":-}", "{\"a\":tru}", "{\"a\":\"\\x\"}", "{\"a\":\"\\ud800\"}",
		"{\"a\":\"\\udc00x\"}", "{\"a\":\"\x01\"}", "{\"a\":\"\xc3\"}",
		"{\"a\":\"\xc0\xaf\"}", "{\"a\":\"\\u0000\"}", "\xef\xbb\xbf{}",
	};
	GString *deep = g_string_new("{\"a\":");
	E2E_JSON *j;
	gsize i;

	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		j = parse(bad[i]);
		if (j != NULL)
			g_error("parsed: %s", bad[i]);
	}
	for (i = 0; i < 1000; i++)
		g_string_append_c(deep, '[');
	g_assert_null(parse(deep->str));
	g_string_free(deep, TRUE);
}

/* rpe2e.pl keeps erssi's byte strings: JSON::PP writes each byte as a
   character (so "#żaba" is stored as "#Å¼aba"), and reads back any text
   whose characters all fit a byte as those bytes. Real Unicode (repartee
   exports, the migration tool) is turned into UTF-8. */
static void test_json_byte_strings(void)
{
	const char *zaba = "#\xc5\xbc" "aba";
	E2E_JSON *j, *k;
	char *out;

	j = parse("{\"#\xc3\x85\xc2\xbc" "aba\":\"\xc3\x85\xc2\xbc\"}");
	g_assert_nonnull(e2e_json_get(j, zaba));
	g_assert_cmpstr(e2e_json_get_string(j, zaba), ==, "\xc5\xbc");
	out = e2e_json_encode(j, FALSE);
	g_assert_cmpstr(out, ==, "{\"#\xc3\x85\xc2\xbc" "aba\":\"\xc3\x85\xc2\xbc\"}");
	g_free(out);
	e2e_json_free(j);

	/* the migration tool's json.dump (ensure_ascii) and raw UTF-8 */
	j = parse("{\"#\\u017caba\":1, \"x\":\"\xc5\xbc\", \"y\":\"\\u00e9\\u20ac\"}");
	g_assert_nonnull(e2e_json_get(j, zaba));
	g_assert_cmpstr(e2e_json_get_string(j, "x"), ==, "\xc5\xbc");
	g_assert_cmpstr(e2e_json_get_string(j, "y"), ==, "\xc3\xa9\xe2\x82\xac");
	e2e_json_free(j);

	/* characters that all fit a byte stay bytes, exactly as in Perl */
	j = parse("{\"x\":\"\\u00e9\"}");
	g_assert_cmpstr(e2e_json_get_string(j, "x"), ==, "\xe9");
	e2e_json_free(j);

	/* astral characters via a surrogate pair */
	j = parse("{\"x\":\"\\ud83d\\ude00\"}");
	g_assert_cmpstr(e2e_json_get_string(j, "x"), ==, "\xf0\x9f\x98\x80");
	k = e2e_json_copy(j);
	e2e_json_free(j);
	g_assert_cmpstr(e2e_json_get_string(k, "x"), ==, "\xf0\x9f\x98\x80");
	e2e_json_free(k);
}

static void test_json_truthy(void)
{
	E2E_JSON *j = parse("{\"a\":0,\"b\":1,\"c\":\"0\",\"d\":\"\",\"e\":\"no\",\"f\":null,"
	                    "\"g\":true,\"h\":false,\"i\":0.0,\"j\":\"0.0\",\"k\":{},\"l\":[]}");

	g_assert_false(e2e_json_truthy(e2e_json_get(j, "a")));
	g_assert_true(e2e_json_truthy(e2e_json_get(j, "b")));
	g_assert_false(e2e_json_truthy(e2e_json_get(j, "c")));
	g_assert_false(e2e_json_truthy(e2e_json_get(j, "d")));
	g_assert_true(e2e_json_truthy(e2e_json_get(j, "e")));
	g_assert_false(e2e_json_truthy(e2e_json_get(j, "f")));
	g_assert_true(e2e_json_truthy(e2e_json_get(j, "g")));
	g_assert_false(e2e_json_truthy(e2e_json_get(j, "h")));
	g_assert_false(e2e_json_truthy(e2e_json_get(j, "i")));
	g_assert_true(e2e_json_truthy(e2e_json_get(j, "j")));
	g_assert_true(e2e_json_truthy(e2e_json_get(j, "k")));
	g_assert_true(e2e_json_truthy(e2e_json_get(j, "l")));
	g_assert_false(e2e_json_truthy(e2e_json_get(j, "missing")));
	g_assert_cmpint(e2e_json_get_int(j, "b", 7), ==, 1);
	g_assert_cmpint(e2e_json_get_int(j, "missing", 7), ==, 7);
	e2e_json_free(j);
}

/* ---- the keyring file ---- */

static void test_absent(void)
{
	E2E_LOAD_STATUS status;
	char *error = NULL;
	E2E_JSON *kr;

	setup();
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	/* rpe2e.pl's empty_keyring */
	g_assert_cmpint(e2e_json_get(kr, "peers")->type, ==, E2E_JSON_OBJECT);
	g_assert_cmpint(e2e_json_get(kr, "outgoing_recipients")->type, ==, E2E_JSON_OBJECT);
	g_assert_cmpint(e2e_json_get(kr, "pending_inbound")->type, ==, E2E_JSON_OBJECT);
	g_assert_cmpint(e2e_json_get(kr, "autotrust")->type, ==, E2E_JSON_ARRAY);
	g_assert_cmpint(e2e_json_get(kr, "pending_trust_change")->type, ==, E2E_JSON_ARRAY);
	e2e_json_free(kr);
	remove_tree();
}

static void test_save_load(void)
{
	E2E_LOAD_STATUS status;
	char *error = NULL, *data, *again;
	E2E_JSON *kr, *kr2;
	GStatBuf st;

	setup();
	/* a file written by rpe2e.pl, with a field this version does not know */
	spew(path, "{\"identity\":null,\"peers\":{},\"outgoing\":{},\"incoming\":{},"
	           "\"channels\":{\"#\xc3\x85\xc2\xbc" "aba\":{\"enabled\":1,\"mode\":\"normal\"}},"
	           "\"pending\":{},\"autotrust\":[],\"outgoing_recipients\":{},"
	           "\"pending_inbound\":{},\"pending_trust_change\":[],\"future\":[1,{\"x\":2}]}");
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_true(e2e_json_truthy(e2e_json_get(e2e_json_get(e2e_json_get(kr, "channels"),
	                                                        "#\xc5\xbc" "aba"), "enabled")));
	g_assert_true(e2e_keyring_save(path, kr, &error));
	g_assert_cmpint(g_stat(path, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);
	g_assert_cmpint(count_files("keyring.json.tmp"), ==, 0);
	data = slurp(path);
	/* unknown fields kept, names stored as rpe2e.pl stores them */
	g_assert_nonnull(strstr(data, "\"future\":[1,{\"x\":2}]"));
	g_assert_nonnull(strstr(data, "\"#\xc3\x85\xc2\xbc" "aba\""));
	kr2 = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	again = e2e_keyring_snapshot(kr2);
	g_assert_cmpstr(again, ==, data);
	g_free(again);
	g_free(data);
	e2e_json_free(kr);
	e2e_json_free(kr2);
	remove_tree();
}

static void test_corrupt(void)
{
	char *notice = NULL, *aside, *data;
	E2E_JSON *kr;

	setup();
	spew(path, "{\"identity\": {\"pk\": \"trunc");
	kr = e2e_keyring_load(path, &notice);
	g_assert_nonnull(kr);
	g_assert_cmpint(count_files("keyring.json.corrupt-"), ==, 1);
	aside = first_file("keyring.json.corrupt-");
	data = slurp(aside);
	g_assert_cmpstr(data, ==, "{\"identity\": {\"pk\": \"trunc");
	/* the user is told where it went */
	g_assert_nonnull(notice);
	g_assert_nonnull(strstr(notice, aside));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_free(data);
	g_free(aside);
	g_free(notice);
	e2e_json_free(kr);
	remove_tree();
}

static void test_unreadable(void)
{
	E2E_LOAD_STATUS status;
	char *error = NULL, *notice = NULL, *before, *after;
	E2E_JSON *kr;

	if (geteuid() == 0) {
		g_test_skip("running as root, file modes are not enforced");
		return;
	}
	setup();
	spew(path, "{\"identity\":null}");
	before = slurp(path);
	g_assert_cmpint(g_chmod(path, 0), ==, 0);

	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_READ_ERROR);
	g_free(error);
	error = NULL;
	e2e_json_free(kr);

	kr = e2e_keyring_load(path, &notice);
	g_assert_nonnull(notice);
	g_assert_nonnull(strstr(notice, "cannot read keyring"));
	g_free(notice);
	/* and it is not replaced */
	g_assert_false(e2e_keyring_save(path, kr, &error));
	g_assert_nonnull(strstr(error, "keyring NOT saved"));
	g_free(error);
	e2e_json_free(kr);
	g_assert_cmpint(count_files("keyring.json.corrupt-"), ==, 0);
	g_assert_cmpint(g_chmod(path, 0600), ==, 0);
	after = slurp(path);
	g_assert_cmpstr(after, ==, before);
	g_free(before);
	g_free(after);
	remove_tree();
}

static void test_wrong_layout(void)
{
	E2E_LOAD_STATUS status;
	char *error = NULL, *notice = NULL;
	E2E_JSON *kr;

	setup();
	/* a channel config that is no object: rpe2e.pl refused to send then;
	   such a file is not used (so the gate refuses), never rewritten */
	spew(path, "{\"channels\":{\"#x\":\"on\"}}");
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_READ_ERROR);
	g_free(error);
	error = NULL;
	e2e_json_free(kr);
	/* valid JSON that is no keyring: kept as it is, nothing saved over it */
	spew(path, "{\"peers\":5}");
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_READ_ERROR);
	g_free(error);
	error = NULL;
	e2e_json_free(kr);
	kr = e2e_keyring_load(path, &notice);
	g_free(notice);
	g_assert_false(e2e_keyring_save(path, kr, &error));
	g_free(error);
	e2e_json_free(kr);
	g_assert_cmpint(count_files("keyring.json.corrupt-"), ==, 0);
	remove_tree();
}

/* rpe2e.t "a failed write (disk full) keeps the old keyring": a child with
   RLIMIT_FSIZE of 4 KiB cannot write the big keyring */
static void test_partial_write(void)
{
	E2E_LOAD_STATUS status;
	char *error = NULL, *before, *after;
	E2E_JSON *kr, *peers;
	pid_t pid;
	int i, wstatus;

	setup();
	kr = e2e_keyring_load_checked(path, &status, &error);
	peers = e2e_json_get(kr, "peers");
	for (i = 0; i < 400; i++) {
		char *fp = g_strdup_printf("%032x", i);
		E2E_JSON *p = e2e_json_new(E2E_JSON_OBJECT);

		e2e_json_set_string(p, "pk", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
		e2e_json_set(peers, fp, p);
		g_free(fp);
	}
	g_assert_true(e2e_keyring_save(path, kr, &error));
	before = slurp(path);
	g_assert_cmpuint(strlen(before), >, 8192);

	pid = fork();
	g_assert_cmpint(pid, >=, 0);
	if (pid == 0) {
		struct rlimit rl = { 4096, 4096 };

		signal(SIGXFSZ, SIG_IGN);
		setrlimit(RLIMIT_FSIZE, &rl);
		e2e_json_set_string(peers, "ffffffffffffffffffffffffffffffff", "y");
		_exit(e2e_keyring_save(path, kr, &error) ? 1 : 0);
	}
	g_assert_cmpint(waitpid(pid, &wstatus, 0), ==, pid);
	g_assert_true(WIFEXITED(wstatus));
	g_assert_cmpint(WEXITSTATUS(wstatus), ==, 0);	/* the save failed */
	after = slurp(path);
	g_assert_cmpstr(after, ==, before);
	g_assert_cmpint(count_files("keyring.json.tmp"), ==, 0);
	g_free(before);
	g_free(after);
	e2e_json_free(kr);
	remove_tree();
}

static void test_backup_and_new_file(void)
{
	char *error = NULL, *bak, *data, *link, *victim;
	GStatBuf st;

	setup();
	/* no keyring yet: nothing to back up */
	bak = e2e_keyring_backup(path, &error);
	g_assert_cmpstr(bak, ==, "");
	g_free(bak);
	spew(path, "{\"identity\":null}");
	bak = e2e_keyring_backup(path, &error);
	g_assert_true(g_str_has_prefix(bak, path));
	g_assert_nonnull(strstr(bak, ".bak-"));
	data = slurp(bak);
	g_assert_cmpstr(data, ==, "{\"identity\":null}");
	g_assert_cmpint(g_stat(bak, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);
	g_free(data);
	g_free(bak);

	/* /e2e export: created 0600, never over a file or through a symlink */
	victim = g_build_filename(dir, "victim", NULL);
	link = g_build_filename(dir, "link", NULL);
	spew(victim, "precious");
	g_assert_cmpint(symlink(victim, link), ==, 0);
	g_assert_false(e2e_write_new_file(link, "x", 1, &error));
	g_free(error);
	error = NULL;
	g_assert_false(e2e_write_new_file(victim, "x", 1, &error));
	g_assert_nonnull(strstr(error, "File exists"));
	g_free(error);
	error = NULL;
	data = slurp(victim);
	g_assert_cmpstr(data, ==, "precious");
	g_free(data);
	g_unlink(link);
	g_free(link);
	g_free(victim);
	victim = g_build_filename(dir, "new.json", NULL);
	g_assert_true(e2e_write_new_file(victim, "{}", 2, &error));
	g_assert_cmpint(g_stat(victim, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);
	g_free(victim);
	remove_tree();
}

/* what contrib/rpe2e/rpe2e-from-repartee writes: json.dump(indent=3,
   sort_keys=True), \u escapes, a final newline */
static char *migration_tool_text(const char *fp, const char *pkb, const char *skb)
{
	return g_strdup_printf(
		"{\n"
		"   \"autotrust\": [],\n"
		"   \"channels\": {\n"
		"      \"#\\u017caba\": {\n"
		"         \"enabled\": 0,\n"
		"         \"mode\": \"normal\"\n"
		"      }\n"
		"   },\n"
		"   \"identity\": {\n"
		"      \"created_at\": 1700000000,\n"
		"      \"fp\": \"%s\",\n"
		"      \"pk\": \"%s\",\n"
		"      \"sk\": \"%s\"\n"
		"   },\n"
		"   \"incoming\": {},\n"
		"   \"outgoing\": {},\n"
		"   \"outgoing_recipients\": {},\n"
		"   \"peers\": {},\n"
		"   \"pending\": {},\n"
		"   \"pending_inbound\": {},\n"
		"   \"pending_trust_change\": []\n"
		"}\n", fp, pkb, skb);
}

static void test_migration_tool_file(void)
{
	E2E_LOAD_STATUS status;
	E2E_IDENTITY id;
	E2E_JSON *kr;
	unsigned char seed[32], pk[32], sk[64];
	char *error = NULL, *pkb, *skb, *fp, *text;
	gboolean created;

	setup();
	memset(seed, 0x41, sizeof(seed));
	g_assert_true(e2e_ed25519_seed_keypair(seed, pk, sk));
	pkb = e2e_b64_encode(pk, 32);
	skb = e2e_b64_encode(sk, 64);
	fp = e2e_fingerprint_hex(pk);
	text = migration_tool_text(fp, pkb, skb);
	spew(path, text);
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	/* the same identity, so peers see no new key */
	g_assert_true(e2e_identity_get(kr, &id, FALSE, &created, &error));
	g_assert_cmpmem(id.pk, 32, pk, 32);
	g_assert_cmpstr(id.fp_hex, ==, fp);
	g_assert_nonnull(e2e_json_get(e2e_json_get(kr, "channels"), "#\xc5\xbc" "aba"));
	e2e_identity_wipe(&id);
	e2e_json_free(kr);
	g_free(text);
	g_free(pkb);
	g_free(skb);
	g_free(fp);
	remove_tree();
}

/* once a corrupt keyring is moved aside, nothing is sent and nothing
   saved until the user starts a new one */
static void test_corrupt_fails_closed(void)
{
	E2E_LOAD_STATUS status;
	E2E_GATE_RESULT g;
	E2E_IDENTITY id;
	gboolean created;
	char *notice = NULL, *error = NULL;
	E2E_JSON *kr, *fresh;

	setup();
	spew(path, "{\"channels\":{\"#secret\":{\"enabled\":1,\"mode\":\"normal\"}},");
	kr = e2e_keyring_load(path, &notice);
	g_assert_nonnull(notice);
	e2e_json_free(kr);
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));

	/* the gate reads with load_checked: not "no keyring" */
	kr = e2e_keyring_load_checked(path, &status, &error);
	e2e_gate_decide(kr, status == E2E_LOAD_OK, NULL, NULL, "#secret", "hello", NULL, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_REFUSE);
	e2e_gate_result_clear(&g);
	g_assert_cmpint(status, ==, E2E_LOAD_LOST);
	g_free(error);
	error = NULL;
	/* and the user is told what to do */
	g_assert_nonnull(strstr(notice, "/e2e reset"));
	g_free(notice);
	notice = NULL;
	/* nothing saved over it - a new identity would hide the loss */
	g_assert_true(e2e_identity_get(kr, &id, TRUE, &created, &error));
	e2e_identity_wipe(&id);
	g_assert_false(e2e_keyring_save(path, kr, &error));
	g_assert_nonnull(strstr(error, "/e2e reset"));
	g_free(error);
	error = NULL;
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	/* still lost on the next load (a restart, /upgrade) */
	e2e_json_free(kr);
	kr = e2e_keyring_load(path, &notice);
	g_assert_nonnull(notice);
	g_free(notice);
	e2e_json_free(kr);

	/* /e2e reset: a new keyring, and everything works again */
	fresh = e2e_keyring_new();
	g_assert_true(e2e_keyring_start_fresh(path, fresh, &error));
	e2e_json_free(fresh);
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_true(e2e_keyring_save(path, kr, &error));
	e2e_gate_decide(kr, status == E2E_LOAD_OK, NULL, NULL, "#secret", "hello", NULL, NULL, &g);
	g_assert_cmpint(g.action, ==, E2E_GATE_PASS);
	e2e_gate_result_clear(&g);
	/* only while there is no keyring */
	fresh = e2e_keyring_new();
	g_assert_false(e2e_keyring_start_fresh(path, fresh, &error));
	g_free(error);
	e2e_json_free(fresh);
	e2e_json_free(kr);
	remove_tree();
}

/* what this client adds to the file ("accepted", "seen_rekeys") is kept
   by a load and save, as rpe2e.pl keeps members it does not know */
static void test_own_members(void)
{
	E2E_LOAD_STATUS status;
	char *error = NULL, *data;
	E2E_JSON *kr;

	setup();
	spew(path, "{\"identity\":null,\"accepted\":{\"a@b|#c\":{\"fp\":\"00\",\"accepted_at\":1,\"by\":\"accept\"}},"
	           "\"seen_rekeys\":{\"00|#c\":{\"AAAA\":1}}}");
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_true(e2e_keyring_save(path, kr, &error));
	data = slurp(path);
	g_assert_nonnull(strstr(data, "\"accepted\":{\"a@b|#c\":{\"accepted_at\":1,\"by\":\"accept\",\"fp\":\"00\"}}"));
	g_assert_nonnull(strstr(data, "\"seen_rekeys\":{\"00|#c\":{\"AAAA\":1}}"));
	g_free(data);
	e2e_json_free(kr);
	/* an rpe2e.pl file without them: empty ones */
	spew(path, "{\"identity\":null}");
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_cmpint(e2e_json_get(kr, "accepted")->type, ==, E2E_JSON_OBJECT);
	g_assert_cmpint(e2e_json_get(kr, "seen_rekeys")->type, ==, E2E_JSON_OBJECT);
	e2e_json_free(kr);
	remove_tree();
}

/* ---- old keyrings and the network scope ---- */

#define SK_A "ZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmY="
#define SK_B "ERERERERERERERERERERERERERERERERERERERERERE="
#define FP_B "b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0"

/* an export of repartee, whose contexts are "<network>" U+001F "#chan" */
static const char repartee_export[] =
	"{\"version\":1,\"exportedAt\":1791564000,\"identity\":null,"
	"\"peers\":[{\"fingerprint\":\"" FP_B "\",\"pk\":\"" SK_B "\",\"status\":\"trusted\"}],"
	"\"incomingSessions\":[{\"handle\":\"bob@127.0.0.1\",\"channel\":\"IRCnet\\u001f#test\","
	  "\"fingerprint\":\"" FP_B "\",\"sk\":\"" SK_B "\",\"status\":\"trusted\",\"createdAt\":1791564000}],"
	"\"outgoingSessions\":[{\"channel\":\"IRCnet\\u001f#test\",\"sk\":\"" SK_A "\","
	  "\"createdAt\":1791564000,\"pendingRotation\":false}],"
	"\"channels\":[{\"channel\":\"IRCnet\\u001f#test\",\"enabled\":true,\"mode\":\"normal\"}],"
	"\"autotrust\":[],"
	"\"outgoingRecipients\":[{\"channel\":\"IRCnet\\u001f#test\",\"handle\":\"bob@127.0.0.1\","
	  "\"fingerprint\":\"" FP_B "\",\"first_sent_at\":1791564000}]}";

static E2E_CTX_KIND lookup_kind(const E2E_JSON *kr, const char *label, gboolean only,
                                const char *wire)
{
	E2E_NET net = { (char *) label, E2E_CASEMAP_RFC1459, only };
	char *ctx = NULL;
	E2E_CTX_KIND kind = e2e_ctx_lookup(kr, &net, wire, &ctx);

	g_free(ctx);
	return kind;
}

static E2E_JSON *import_text(const char *text)
{
	char *error = NULL;
	E2E_JSON *doc = parse(text), *kr;

	g_assert_nonnull(doc);
	kr = e2e_keyring_import(doc, &error);
	g_assert_null(error);
	g_assert_nonnull(kr);
	e2e_json_free(doc);
	return kr;
}

/* the files the module meets load as they are: rpe2e.pl's, the migration
   tool's, and a keyring imported from a repartee export */
static void test_legacy_files_load(void)
{
	E2E_LOAD_STATUS status;
	E2E_JSON *kr;
	unsigned char seed[32], pk[32], sk[64];
	char *error = NULL, *pkb, *skb, *fp, *text;

	setup();
	/* (a) rpe2e.pl: unscoped, erssi's byte strings ("#żaba" as JSON::PP
	   writes its bytes) */
	spew(path, "{\"identity\":null,"
	           "\"channels\":{\"#test\":{\"enabled\":1,\"mode\":\"normal\"},"
	                        "\"#\xc3\x85\xc2\xbc" "aba\":{\"enabled\":1,\"mode\":\"quiet\"}},"
	           "\"outgoing\":{\"#test\":{\"sk\":\"" SK_A "\",\"created_at\":1791564000,\"pending_rotation\":0}},"
	           "\"incoming\":{\"bob@127.0.0.1|#test\":{\"fp\":\"" FP_B "\",\"sk\":\"" SK_B "\",\"status\":\"trusted\",\"created_at\":1791564000}},"
	           "\"peers\":{},\"pending\":{},\"autotrust\":[],\"outgoing_recipients\":{},"
	           "\"pending_inbound\":{},\"pending_trust_change\":[]}");
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_false(e2e_keyring_has_scoped(kr));
	g_assert_cmpint(lookup_kind(kr, "IRCnet", TRUE, "#test"), ==, E2E_CTX_LEGACY);
	g_assert_cmpint(lookup_kind(kr, "IRCnet", TRUE, "#\xc5\xbc" "aba"), ==, E2E_CTX_LEGACY);
	g_assert_cmpint(lookup_kind(kr, "IRCnet", FALSE, "#test"), ==, E2E_CTX_BLOCKED);
	e2e_json_free(kr);

	/* (b) contrib/rpe2e/rpe2e-from-repartee */
	memset(seed, 0x41, sizeof(seed));
	g_assert_true(e2e_ed25519_seed_keypair(seed, pk, sk));
	pkb = e2e_b64_encode(pk, 32);
	skb = e2e_b64_encode(sk, 64);
	fp = e2e_fingerprint_hex(pk);
	text = migration_tool_text(fp, pkb, skb);
	spew(path, text);
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_false(e2e_keyring_has_scoped(kr));
	g_assert_cmpint(lookup_kind(kr, "IRCnet", TRUE, "#\xc5\xbc" "aba"), ==, E2E_CTX_LEGACY);
	e2e_json_free(kr);
	g_free(text);
	g_free(pkb);
	g_free(skb);
	g_free(fp);

	/* (c) a repartee export, imported and saved: IRCnet's #test, not
	   Libera's */
	kr = import_text(repartee_export);
	g_assert_true(e2e_keyring_save(path, kr, &error));
	e2e_json_free(kr);
	kr = e2e_keyring_load_checked(path, &status, &error);
	g_assert_cmpint(status, ==, E2E_LOAD_OK);
	g_assert_true(e2e_keyring_has_scoped(kr));
	g_assert_cmpint(lookup_kind(kr, "IRCnet", FALSE, "#test"), ==, E2E_CTX_SCOPED);
	g_assert_cmpint(lookup_kind(kr, "IRCnet", TRUE, "#test"), ==, E2E_CTX_SCOPED);
	g_assert_cmpint(lookup_kind(kr, "Libera", FALSE, "#test"), ==, E2E_CTX_NEW);
	g_assert_cmpint(lookup_kind(kr, "Libera", TRUE, "#test"), ==, E2E_CTX_NEW);
	g_assert_cmpstr(e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "incoming"),
	                                                 "bob@127.0.0.1|IRCnet\x1f#test"), "sk"), ==, SK_B);
	g_assert_cmpstr(e2e_json_get_string(e2e_json_get(e2e_json_get(kr, "outgoing"),
	                                                 "IRCnet\x1f#test"), "sk"), ==, SK_A);
	e2e_json_free(kr);
	remove_tree();
}

/* /e2e export and /e2e import keep a network's contexts as they are; the
   file holds U+001F as \u001f, never as a raw byte */
static void test_legacy_export_keeps_scope(void)
{
	static const char *const stores[] = {
		"channels", "outgoing", "incoming", "outgoing_recipients", "peers", NULL
	};
	E2E_JSON *kr = import_text(repartee_export), *doc, *back;
	char *text, *a, *b;
	int i;

	e2e_json_set_string(e2e_json_get(e2e_json_get(kr, "channels"), "IRCnet\x1f#test"),
	                    "adopted", "IRCnet");
	doc = e2e_keyring_export(kr);
	text = e2e_json_encode(doc, TRUE);
	g_assert_null(strchr(text, '\x1f'));
	g_assert_nonnull(strstr(text, "IRCnet\\u001f#test"));
	back = import_text(text);
	for (i = 0; stores[i] != NULL; i++) {
		a = e2e_json_encode(e2e_json_get(kr, stores[i]), FALSE);
		b = e2e_json_encode(e2e_json_get(back, stores[i]), FALSE);
		g_assert_cmpstr(a, ==, b);
		g_free(a);
		g_free(b);
	}
	g_assert_nonnull(e2e_json_get(e2e_json_get(back, "channels"), "IRCnet\x1f#test"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(back, "incoming"), "bob@127.0.0.1|IRCnet\x1f#test"));
	g_assert_nonnull(e2e_json_get(e2e_json_get(back, "outgoing_recipients"),
	                              "IRCnet\x1f#test|bob@127.0.0.1"));
	g_free(text);
	e2e_json_free(back);
	e2e_json_free(doc);
	e2e_json_free(kr);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/e2e/json/roundtrip", test_json_roundtrip);
	g_test_add_func("/e2e/json/rejects", test_json_rejects);
	g_test_add_func("/e2e/json/byte-strings", test_json_byte_strings);
	g_test_add_func("/e2e/json/truthy", test_json_truthy);
	g_test_add_func("/e2e/keyring/absent", test_absent);
	g_test_add_func("/e2e/keyring/save-load", test_save_load);
	g_test_add_func("/e2e/keyring/corrupt", test_corrupt);
	g_test_add_func("/e2e/keyring/corrupt-fails-closed", test_corrupt_fails_closed);
	g_test_add_func("/e2e/keyring/own-members", test_own_members);
	g_test_add_func("/e2e/keyring/unreadable", test_unreadable);
	g_test_add_func("/e2e/keyring/wrong-layout", test_wrong_layout);
	g_test_add_func("/e2e/keyring/partial-write", test_partial_write);
	g_test_add_func("/e2e/keyring/backup-and-new-file", test_backup_and_new_file);
	g_test_add_func("/e2e/keyring/migration-tool-file", test_migration_tool_file);
	g_test_add_func("/e2e/legacy/files-load", test_legacy_files_load);
	g_test_add_func("/e2e/legacy/export-keeps-scope", test_legacy_export_keeps_scope);

	return g_test_run();
}
