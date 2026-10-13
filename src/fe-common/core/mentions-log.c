/*
 mentions-log.c : pure helpers for the mentions log

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#include "module.h"
#include <irssi/src/core/misc.h>
#include <irssi/src/core/capsicum.h>
#include <irssi/src/fe-common/core/mentions-log.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* Every C0 control (CR, LF, TAB and ESC included), DEL, every UTF-8 encoded
   C1 control (U+0080-U+009F) and every byte 0x80-0x9F that is not part of
   a valid UTF-8 character (an 8-bit C1 control) becomes one space, so a log
   line is one line and cannot drive a terminal that shows it. Valid UTF-8
   and other bytes are kept as they are. */
static char *sanitize_field(const char *s, const char *fallback)
{
	const unsigned char *in, *end;
	char *out, *o;
	gunichar c;
	int len;

	in = (const unsigned char *) (s != NULL ? s : fallback);
	end = in + strlen((const char *) in);
	out = o = g_malloc(end - in + 1);
	while (in < end) {
		if (*in < 0x20 || *in == 0x7f) {
			*o++ = ' ';
			in++;
		} else if (*in < 0x80) {
			*o++ = (char) *in++;
		} else {
			c = g_utf8_get_char_validated((const char *) in, end - in);
			if (c == (gunichar) -1 || c == (gunichar) -2) {
				/* not UTF-8: only the 8-bit C1 range is changed */
				*o++ = *in >= 0x80 && *in <= 0x9f ? ' ' : (char) *in;
				in++;
				continue;
			}
			len = g_unichar_to_utf8(c, NULL);
			if (c >= 0x80 && c <= 0x9f) {
				*o++ = ' ';
			} else {
				memcpy(o, in, len);
				o += len;
			}
			in += len;
		}
	}
	*o = '\0';
	return out;
}

char *mentions_log_line(time_t when, const char *tag, const char *where, const char *nick,
                        const char *text)
{
	char stamp[32];
	struct tm tm;
	char *t, *w, *n, *x, *line;

	if (localtime_r(&when, &tm) == NULL ||
	    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm) == 0)
		g_strlcpy(stamp, "0000-00-00 00:00:00", sizeof(stamp));

	t = sanitize_field(tag, "-");
	w = sanitize_field(where, "-");
	n = sanitize_field(nick, "?");
	x = sanitize_field(text, "");
	g_strstrip(x);

	line = g_strdup_printf("%s [%s] %s <%s> %s\n", stamp, t, w, n, x);
	g_free(t);
	g_free(w);
	g_free(n);
	g_free(x);
	return line;
}

gboolean mentions_log_append(const char *path, const char *line, GError **error)
{
	char *dir;
	int fd, err, flags;
	struct stat st;
	const char *p;
	size_t left;

	g_return_val_if_fail(path != NULL && line != NULL, FALSE);

	dir = g_path_get_dirname(path);
#ifdef HAVE_CAPSICUM
	capsicum_mkdir_with_parents_wrapper(dir, 0700);
#else
	g_mkdir_with_parents(dir, 0700);
#endif
	g_free(dir);

	/* O_NONBLOCK: a FIFO without a reader must not block the client; a
	   non-regular file is refused below before anything is written */
#ifdef HAVE_CAPSICUM
	fd = capsicum_open_wrapper(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NONBLOCK,
	                           0600);
#else
	fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NONBLOCK, 0600);
#endif
	if (fd == -1) {
		err = errno;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		            "cannot open %s: %s", path, g_strerror(err));
		return FALSE;
	}

	if (fstat(fd, &st) != 0) {
		err = errno;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		            "cannot stat %s: %s", path, g_strerror(err));
		close(fd);
		return FALSE;
	}
	if (!S_ISREG(st.st_mode)) {
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "%s is not a regular file", path);
		close(fd);
		return FALSE;
	}
	flags = fcntl(fd, F_GETFL);
	if (flags != -1)
		(void) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	if (st.st_uid == getuid() && (st.st_mode & 07777) != 0600)
		(void) fchmod(fd, 0600);

	p = line;
	left = strlen(line);
	while (left > 0) {
		ssize_t n = write(fd, p, left);

		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			err = n < 0 ? errno : EIO;
			g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
			            "cannot write %s: %s", path, g_strerror(err));
			close(fd);
			return FALSE;
		}
		p += n;
		left -= (size_t) n;
	}

	if (close(fd) != 0) {
		err = errno;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		            "cannot close %s: %s", path, g_strerror(err));
		return FALSE;
	}
	return TRUE;
}

char *mentions_log_path(const char *setting, const char *irssi_dir)
{
	char *expanded;

	if (setting == NULL || *setting == '\0') {
		if (irssi_dir == NULL)
			return NULL;
		return g_build_filename(irssi_dir, "logs", "mentions.log", NULL);
	}

	/* '~' and a relative file name: a relative one is in the irssi dir, not
	   in the directory erssi happened to be started from */
	expanded = convert_home(setting);
	if (irssi_dir != NULL && !g_path_is_absolute(expanded)) {
		char *joined = g_build_filename(irssi_dir, expanded, NULL);

		g_free(expanded);
		expanded = joined;
	}
	return expanded;
}

gboolean mentions_network_excluded(const char *chatnet, const char *list)
{
	char **tokens, **t;
	gboolean found = FALSE;

	if (chatnet == NULL || *chatnet == '\0' || list == NULL)
		return FALSE;

	tokens = g_strsplit_set(list, " ,", -1);
	for (t = tokens; *t != NULL; t++) {
		if (**t != '\0' && g_ascii_strcasecmp(*t, chatnet) == 0) {
			found = TRUE;
			break;
		}
	}
	g_strfreev(tokens);
	return found;
}

gboolean mentions_is_server_nick(const char *nick)
{
	return nick == NULL || *nick == '\0' || strchr(nick, '.') != NULL;
}
