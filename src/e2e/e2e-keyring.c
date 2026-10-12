/*
 e2e-keyring.c : keyring.json on disk, in rpe2e.pl 0.2.0's format

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 The load/save rules of rpe2e.pl 0.2.0, made stricter (the script may
 replace a file it failed to read): an existing file that cannot be read
 is never replaced, a corrupt one is kept aside, a write never leaves a
 truncated keyring behind. After a corrupt keyring was moved aside,
 the missing file does not mean "no E2E anywhere" (rpe2e.pl started fresh
 and the gate let everything out in clear text) - the keyring is LOST
 until /e2e reset starts a new one (e2e_keyring_start_fresh).
*/

#include "e2e-keyring.h"
#include "e2e-crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* far above any real keyring; only stops a wrong path from eating memory */
#define MAX_KEYRING_SIZE (64 * 1024 * 1024)

static const char *const object_members[] = {
	"peers", "outgoing", "incoming", "channels", "pending",
	"outgoing_recipients", "pending_inbound",
	/* this client's own (rpe2e.pl keeps members it does not know) */
	"accepted", "seen_rekeys", NULL
};
static const char *const array_members[] = {
	"autotrust", "pending_trust_change", NULL
};

E2E_JSON *e2e_keyring_new(void)
{
	E2E_JSON *kr = e2e_json_new(E2E_JSON_OBJECT);
	int i;

	e2e_json_set(kr, "identity", e2e_json_new(E2E_JSON_NULL));
	for (i = 0; object_members[i] != NULL; i++)
		e2e_json_set(kr, object_members[i], e2e_json_new(E2E_JSON_OBJECT));
	for (i = 0; array_members[i] != NULL; i++)
		e2e_json_set(kr, array_members[i], e2e_json_new(E2E_JSON_ARRAY));
	return kr;
}

/* missing members are added as rpe2e.pl's "//= {}" does; a member of the
   wrong type would make every later lookup guess, so such a file is not
   used (and, like an unreadable one, never overwritten) */
static gboolean normalize(E2E_JSON *kr, char **error)
{
	E2E_JSON *node;
	GPtrArray *keys;
	int i;

	node = e2e_json_get(kr, "identity");
	if (node == NULL)
		e2e_json_set(kr, "identity", e2e_json_new(E2E_JSON_NULL));
	else if (node->type != E2E_JSON_NULL && node->type != E2E_JSON_OBJECT) {
		*error = g_strdup("unexpected layout: identity is not an object");
		return FALSE;
	}
	for (i = 0; object_members[i] != NULL; i++) {
		node = e2e_json_get(kr, object_members[i]);
		if (node == NULL || node->type == E2E_JSON_NULL)
			e2e_json_set(kr, object_members[i], e2e_json_new(E2E_JSON_OBJECT));
		else if (node->type != E2E_JSON_OBJECT) {
			*error = g_strdup_printf("unexpected layout: %s is not an object",
			                         object_members[i]);
			return FALSE;
		}
	}
	/* a channel config that is no object: rpe2e.pl failed (and refused to
	   send) on it; reading it as "off" would send in clear text */
	keys = e2e_json_keys(e2e_json_get(kr, "channels"));
	for (i = 0; i < (int) keys->len; i++) {
		node = e2e_json_get(e2e_json_get(kr, "channels"), g_ptr_array_index(keys, i));
		if (node->type != E2E_JSON_OBJECT && node->type != E2E_JSON_NULL) {
			*error = g_strdup_printf("unexpected layout: channels/%s is not an object",
			                         (char *) g_ptr_array_index(keys, i));
			g_ptr_array_unref(keys);
			return FALSE;
		}
	}
	g_ptr_array_unref(keys);
	for (i = 0; array_members[i] != NULL; i++) {
		node = e2e_json_get(kr, array_members[i]);
		if (node == NULL || node->type == E2E_JSON_NULL)
			e2e_json_set(kr, array_members[i], e2e_json_new(E2E_JSON_ARRAY));
		else if (node->type != E2E_JSON_ARRAY) {
			*error = g_strdup_printf("unexpected layout: %s is not a list",
			                         array_members[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/* the whole file, or NULL with errno set */
static char *read_file(const char *path, size_t *len)
{
	GString *data;
	char buf[8192];
	ssize_t n;
	int fd, saved;

	struct stat st;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	/* sized up front: a grown buffer would leave copies of the keys behind
	   in freed memory */
	data = g_string_sized_new(fstat(fd, &st) == 0 && st.st_size > 0 &&
	                          st.st_size < MAX_KEYRING_SIZE ? (gsize) st.st_size + 1 : 8192);
	for (;;) {
		n = read(fd, buf, sizeof(buf));
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			goto fail;
		if (n == 0)
			break;
		g_string_append_len(data, buf, n);
		if (data->len > MAX_KEYRING_SIZE) {
			errno = EFBIG;
			goto fail;
		}
	}
	close(fd);
	e2e_wipe(buf, sizeof(buf));
	*len = data->len;
	return g_string_free(data, FALSE);
fail:
	saved = errno;
	close(fd);
	e2e_wipe(data->str, data->len);
	g_string_free(data, TRUE);
	errno = saved;
	return NULL;
}

/* a keyring.json.corrupt-* next to path: one was moved aside */
static gboolean corrupt_one_aside(const char *path)
{
	char *dir = g_path_get_dirname(path), *base = g_path_get_basename(path), *prefix;
	const char *name;
	gboolean found = FALSE;
	GDir *d;

	prefix = g_strconcat(base, ".corrupt-", NULL);
	d = g_dir_open(dir, 0, NULL);
	while (d != NULL && !found && (name = g_dir_read_name(d)) != NULL)
		found = g_str_has_prefix(name, prefix);
	if (d != NULL)
		g_dir_close(d);
	g_free(prefix);
	g_free(base);
	g_free(dir);
	return found;
}

gboolean e2e_keyring_lost(const char *path)
{
	struct stat st;

	return stat(path, &st) != 0 && errno == ENOENT && corrupt_one_aside(path);
}

E2E_JSON *e2e_keyring_load_checked(const char *path, E2E_LOAD_STATUS *status, char **error)
{
	struct stat st;
	const char *perr = NULL;
	E2E_JSON *kr;
	char *data;
	size_t len;

	*status = E2E_LOAD_OK;
	if (stat(path, &st) != 0) {
		if (errno == ENOENT && corrupt_one_aside(path)) {
			*status = E2E_LOAD_LOST;
			*error = g_strdup("the corrupt keyring was moved aside");
			return e2e_keyring_new();
		}
		if (errno == ENOENT)
			return e2e_keyring_new();
		/* rpe2e.pl's -e would call this "absent"; a path that cannot even
		   be looked at is reported instead */
		*status = E2E_LOAD_READ_ERROR;
		*error = g_strdup(g_strerror(errno));
		return e2e_keyring_new();
	}
	data = read_file(path, &len);
	if (data == NULL) {
		*status = E2E_LOAD_READ_ERROR;
		*error = g_strdup(g_strerror(errno));
		return e2e_keyring_new();
	}
	kr = e2e_json_parse(data, len, &perr);
	e2e_wipe(data, len);
	g_free(data);
	if (kr == NULL) {
		*status = E2E_LOAD_PARSE_ERROR;
		*error = g_strdup("not valid JSON");
		return e2e_keyring_new();
	}
	if (!normalize(kr, error)) {
		*status = E2E_LOAD_READ_ERROR;
		e2e_json_free(kr);
		return e2e_keyring_new();
	}
	return kr;
}

E2E_JSON *e2e_keyring_load(const char *path, char **notice)
{
	E2E_LOAD_STATUS status;

	return e2e_keyring_load_status(path, &status, notice);
}

E2E_JSON *e2e_keyring_load_status(const char *path, E2E_LOAD_STATUS *status, char **notice)
{
	char *error = NULL, *aside;
	E2E_JSON *kr;
	int i;

	*notice = NULL;
	kr = e2e_keyring_load_checked(path, status, &error);
	if (*status == E2E_LOAD_PARSE_ERROR) {
		/* repartee's script started fresh here and the next save wrote a
		   new identity over the real file; keep it for the user */
		aside = g_strdup_printf("%s.corrupt-%ld", path, (long) time(NULL));
		for (i = 1; g_file_test(aside, G_FILE_TEST_EXISTS); i++) {
			g_free(aside);
			aside = g_strdup_printf("%s.corrupt-%ld.%d", path, (long) time(NULL), i);
		}
		if (rename(path, aside) == 0) {
			*status = E2E_LOAD_LOST;
			*notice = g_strdup_printf("keyring %s is not valid JSON — moved aside to %s. " E2E_LOST_HINT,
			                          path, aside);
		} else {
			*notice = g_strdup_printf("keyring %s is not valid JSON and cannot be moved aside (%s) — not saving until it is fixed",
			                          path, g_strerror(errno));
		}
		g_free(aside);
	} else if (*status == E2E_LOAD_LOST) {
		*notice = g_strdup_printf("keyring %s: the corrupt keyring was moved aside (%s.corrupt-*). " E2E_LOST_HINT,
		                          path, path);
	} else if (*status == E2E_LOAD_READ_ERROR) {
		*notice = g_strdup_printf("cannot read keyring %s: %s — E2E state not loaded, nothing is saved until it is readable",
		                          path, error);
	}
	g_free(error);
	return kr;
}

static gboolean write_all(int fd, const char *data, size_t len)
{
	ssize_t n;

	while (len > 0) {
		n = write(fd, data, len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			if (n == 0)
				errno = EIO;
			return FALSE;
		}
		data += n;
		len -= n;
	}
	return TRUE;
}

static void fsync_dir(const char *path)
{
	char *dir = g_path_get_dirname(path);
	int fd = open(dir, O_RDONLY | O_CLOEXEC);

	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
	g_free(dir);
}

/* a new file (O_EXCL, never through a symlink) with data, fsync'ed */
static gboolean write_new(const char *path, const char *data, size_t len, int *err)
{
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) {
		*err = errno;
		return FALSE;
	}
	if (!write_all(fd, data, len) || fsync(fd) != 0) {
		*err = errno;
		close(fd);
		unlink(path);
		return FALSE;
	}
	if (close(fd) != 0) {
		*err = errno;
		unlink(path);
		return FALSE;
	}
	return TRUE;
}

static gboolean keyring_write(const char *path, const E2E_JSON *kr, gboolean fresh,
                              char **error)
{
	struct stat st;
	E2E_LOAD_STATUS status;
	E2E_JSON *current;
	char *data, *tmp, *err = NULL;
	int fd, saved;
	size_t len;

	if (stat(path, &st) == 0) {
		if (fresh) {
			*error = g_strdup_printf("%s exists — there is nothing to reset", path);
			return FALSE;
		}
	} else if (errno == ENOENT && !fresh && corrupt_one_aside(path)) {
		/* a new keyring here would hide that the state was lost */
		*error = g_strdup("keyring NOT saved: the corrupt keyring was moved aside and E2E state is lost — /e2e reset starts a new one");
		return FALSE;
	}
	if (stat(path, &st) == 0 || errno != ENOENT) {
		current = e2e_keyring_load_checked(path, &status, &err);
		e2e_json_free(current);
		if (status != E2E_LOAD_OK) {
			*error = g_strdup_printf("keyring NOT saved: %s exists but cannot be read (%s) — fix it or move it aside",
			                         path, err);
			g_free(err);
			return FALSE;
		}
	}

	data = e2e_json_encode(kr, FALSE);
	len = strlen(data);
	tmp = g_strdup_printf("%s.tmp.%ld.%ld", path, (long) getpid(), (long) time(NULL));
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) {
		*error = g_strdup_printf("cannot write keyring: %s", g_strerror(errno));
		goto fail;
	}
	/* every write is checked and the data is on disk before the rename:
	   a full disk can no longer put a truncated keyring in place */
	if (!write_all(fd, data, len) || fsync(fd) != 0) {
		saved = errno;
		close(fd);
		unlink(tmp);
		*error = g_strdup_printf("cannot write keyring (%s) — the previous keyring is kept",
		                         g_strerror(saved));
		goto fail;
	}
	if (close(fd) != 0) {
		saved = errno;
		unlink(tmp);
		*error = g_strdup_printf("cannot write keyring (%s) — the previous keyring is kept",
		                         g_strerror(saved));
		goto fail;
	}
	if (rename(tmp, path) != 0) {
		saved = errno;
		unlink(tmp);
		*error = g_strdup_printf("cannot replace keyring: %s", g_strerror(saved));
		goto fail;
	}
	fsync_dir(path);
	e2e_wipe(data, len);
	g_free(data);
	g_free(tmp);
	return TRUE;
fail:
	e2e_wipe(data, len);
	g_free(data);
	g_free(tmp);
	return FALSE;
}

gboolean e2e_keyring_save(const char *path, const E2E_JSON *kr, char **error)
{
	return keyring_write(path, kr, FALSE, error);
}

gboolean e2e_keyring_start_fresh(const char *path, const E2E_JSON *kr, char **error)
{
	return keyring_write(path, kr, TRUE, error);
}

char *e2e_keyring_snapshot(const E2E_JSON *kr)
{
	return e2e_json_encode(kr, FALSE);
}

char *e2e_keyring_backup(const char *path, char **error)
{
	struct stat st;
	char *data, *bak;
	size_t len;
	int err;

	if (stat(path, &st) != 0 && errno == ENOENT)
		return g_strdup("");
	data = read_file(path, &len);
	if (data == NULL) {
		*error = g_strdup(g_strerror(errno));
		return NULL;
	}
	bak = g_strdup_printf("%s.bak-%ld", path, (long) time(NULL));
	if (!write_new(bak, data, len, &err)) {
		*error = g_strdup(g_strerror(err));
		g_free(bak);
		bak = NULL;
	}
	e2e_wipe(data, len);
	g_free(data);
	return bak;
}

gboolean e2e_write_new_file(const char *path, const char *data, size_t len, char **error)
{
	int err;

	if (write_new(path, data, len, &err))
		return TRUE;
	*error = g_strdup(g_strerror(err));
	return FALSE;
}
