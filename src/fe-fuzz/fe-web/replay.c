/*
 replay.c : run a fuzz target over saved inputs, without libFuzzer

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 Linked with one target instead of libFuzzer, so the normal build (any
 compiler) can replay the seed corpus and the inputs of fixed crashes in
 `meson test`. Arguments are files or directories of files.
*/

#include <irssi/src/fe-fuzz/fe-web/fe-web-fuzz.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int replayed = 0;

static void replay_file(const char *path)
{
	gchar *data;
	gsize len;
	GError *error = NULL;

	if (!g_file_get_contents(path, &data, &len, &error)) {
		fprintf(stderr, "replay: %s\n", error->message);
		exit(1);
	}
	LLVMFuzzerTestOneInput((const uint8_t *) data, len);
	g_free(data);
	replayed++;
}

static gint compare_names(gconstpointer a, gconstpointer b)
{
	/* g_ptr_array_sort passes pointers to the elements */
	return strcmp(*(char *const *) a, *(char *const *) b);
}

static void replay_path(const char *path)
{
	GDir *dir;
	GPtrArray *names;
	const char *name;
	guint i;

	if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
		replay_file(path);
		return;
	}

	dir = g_dir_open(path, 0, NULL);
	if (dir == NULL) {
		fprintf(stderr, "replay: cannot open %s\n", path);
		exit(1);
	}
	names = g_ptr_array_new_with_free_func(g_free);
	while ((name = g_dir_read_name(dir)) != NULL)
		if (*name != '.')
			g_ptr_array_add(names, g_build_filename(path, name, NULL));
	g_dir_close(dir);

	/* the same order every run */
	g_ptr_array_sort(names, compare_names);
	for (i = 0; i < names->len; i++)
		replay_path(g_ptr_array_index(names, i));
	g_ptr_array_free(names, TRUE);
}

int main(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s file-or-dir...\n", argv[0]);
		return 2;
	}
	LLVMFuzzerInitialize(&argc, &argv);
	for (i = 1; i < argc; i++)
		replay_path(argv[i]);
	printf("replayed %d inputs\n", replayed);
	return replayed > 0 ? 0 : 1;
}
