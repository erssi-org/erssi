/*
 fe-expandos.c : irssi

    Copyright (C) 2000 Timo Sirainen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    
    ---
    
    Modifications for erssi:
    Copyright (C) 2024-2025 erssi-org team
    Lead Developer: Jerzy (kofany) Dąbrowski <https://github.com/kofany>
*/

#include "module.h"
#include <irssi/src/core/expandos.h>
#include <irssi/src/fe-common/core/fe-windows.h>
#include <irssi/src/core/settings.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/commands.h>
#include <time.h>

/* Nick column context variables */
static char *current_nick = NULL;
static char *current_mode = NULL;
static gboolean nick_context_valid = FALSE;

/* Hash-based nick coloring system */
typedef struct {
	char *nick;
	int color_index;
	time_t last_seen;
} nick_color_entry;

typedef struct {
	GHashTable *nick_colors; /* nick -> nick_color_entry */
	char *channel_name;
	char *server_tag;
} channel_color_context;

static GHashTable *channel_contexts = NULL; /* "server:channel" -> channel_color_context */

/* Window ref# */
static char *expando_winref(SERVER_REC *server, void *item, int *free_ret)
{
	if (active_win == NULL)
		return "";

	*free_ret = TRUE;
	return g_strdup_printf("%d", active_win->refnum);
}

/* Window name */
static char *expando_winname(SERVER_REC *server, void *item, int *free_ret)
{
	if (active_win == NULL)
		return "";

	return active_win->name;
}

/* Count only valid nick characters (ignore color codes) */
static int count_nick_chars(const char *str)
{
	int count = 0;
	if (!str)
		return 0;

	for (const char *p = str; *p; p++) {
		/* Alfanumeryczne */
		if (isalnum(*p)) {
			count++;
		}
		/* Specjalne znaki nicka - zgodnie z RFC 2812 i isnickchar z fe-messages.c */
		else if (*p == '`' || *p == '-' || *p == '_' || *p == '[' || *p == ']' ||
		         *p == '{' || *p == '}' || *p == '|' || *p == '\\' || *p == '^' || *p == '~') {
			count++;
		}
		/* Ignoruje kody kolorów %B %N %Y %n itp. */
	}
	return count;
}

/* Nick column align - returns only padding spaces */
static char *expando_nickalign(SERVER_REC *server, void *item, int *free_ret)
{
	int width, mode_chars, nick_chars, total_chars, padding;
	const char *mode;

	/* Gdy wyłączone - zwróć pusty string */
	if (!settings_get_bool("nick_column_enabled")) {
		return "";
	}

	if (!nick_context_valid || !current_nick) {
		return "";
	}

	width = settings_get_int("nick_column_width");
	mode = current_mode ? current_mode : "";

	/* Zawsze 1 miejsce na mode (nawet spacja) */
	mode_chars = strlen(mode) > 0 ? strlen(mode) : 1;
	nick_chars = count_nick_chars(current_nick);
	total_chars = mode_chars + nick_chars;

	if (total_chars > width) {
		padding = 0;
	} else {
		padding = width - total_chars;
	}

	*free_ret = TRUE;
	return g_strnfill(padding, ' ');
}

/* Nick truncated - returns truncated nick with >> indicator */
static char *expando_nicktrunc(SERVER_REC *server, void *item, int *free_ret)
{
	int width, mode_chars, nick_chars, total_chars;
	const char *mode;
	char *result;

	/* Gdy wyłączone - zwróć oryginalny nick */
	if (!settings_get_bool("nick_column_enabled")) {
		return current_nick ? current_nick : "";
	}

	if (!nick_context_valid || !current_nick) {
		return current_nick ? current_nick : "";
	}

	width = settings_get_int("nick_column_width");
	mode = current_mode ? current_mode : "";

	/* Zawsze 1 miejsce na mode (nawet spacja) */
	mode_chars = strlen(mode) > 0 ? strlen(mode) : 1;
	nick_chars = count_nick_chars(current_nick);
	total_chars = mode_chars + nick_chars;

	if (total_chars > width) {
		/* Nick za długi - przytnij z + */
		int available_for_nick = width - mode_chars - 1; /* -1 dla + */
		if (available_for_nick > 0) {
			/* Przytnij nick i dodaj + */
			result = g_strdup_printf("%.*s+", available_for_nick, current_nick);

		} else {
			/* Mode sam za długi */
			result = g_strdup("+");
		}
		*free_ret = TRUE;
		return result;
	} else {
		/* Nick się zmieści - zwróć oryginalny */

		return current_nick;
	}
}

/* Update nick context for expandos */
void update_nick_context(const char *nick, const char *mode)
{
	g_free(current_nick);
	g_free(current_mode);
	current_nick = g_strdup(nick);
	current_mode = g_strdup(mode ? mode : "");
	nick_context_valid = TRUE;
}

/* Clear nick context */
void clear_nick_context(void)
{
	nick_context_valid = FALSE;
}

/* Hash-based nick coloring functions */

static void free_nick_color_entry(nick_color_entry *entry)
{
	if (entry) {
		g_free(entry->nick);
		g_free(entry);
	}
}

static void free_channel_color_context(channel_color_context *ctx)
{
	if (ctx) {
		if (ctx->nick_colors) {
			g_hash_table_destroy(ctx->nick_colors);
		}
		g_free(ctx->channel_name);
		g_free(ctx->server_tag);
		g_free(ctx);
	}
}

static gchar** parse_color_palette(const char *colors_str, int *count)
{
	gchar **colors;
	int valid_count;
	
	colors = g_strsplit(colors_str, " ", -1);
	valid_count = 0;
	
	/* Count valid colors */
	for (int i = 0; colors[i]; i++) {
		if (strlen(colors[i]) == 1 && strchr("krgybmcwKRGYBMCW", colors[i][0])) {
			valid_count++;
		}
	}
	
	/* If no valid colors, use default palette */
	if (valid_count == 0) {
		g_strfreev(colors);
		colors = g_strsplit("g r b m c y G C", " ", -1);
		*count = 8;
	} else {
		*count = valid_count;
	}
	
	return colors;
}

static int hash_nick_to_color_index(const char *nick, const char *channel_key, int palette_size)
{
	char *hash_input;
	unsigned int hash;
	int result;
	
	hash_input = g_strdup_printf("%s:%s", channel_key, nick);
	hash = g_str_hash(hash_input);
	result = hash % palette_size;
	g_free(hash_input);
	return result;
}

static int generate_random_color_index(int old_color, int palette_size)
{
	int new_color;
	
	if (palette_size <= 1)
		return old_color;
	
	/* Generate random color different from old_color */
	do {
		new_color = (time(NULL) + rand()) % palette_size;
	} while (new_color == old_color);
	
	return new_color;
}

static int get_persistent_nick_color(const char *nick, void *item, SERVER_REC *server)
{
	WI_ITEM_REC *witem;
	const char *channel;
	const char *server_tag;
	int palette_size;
	gchar **palette;
	char *channel_key;
	channel_color_context *ctx;
	nick_color_entry *entry;
	int color_index;
	
	
	witem = (WI_ITEM_REC *)item;
	channel = witem ? witem->visible_name : "query";
	server_tag = server ? server->tag : "unknown";
	
	/* Parse color palette */
	palette = parse_color_palette(settings_get_str("nick_hash_colors"), &palette_size);
	
	/* Create channel key */
	channel_key = g_strdup_printf("%s:%s", server_tag, channel);
	
	/* Initialize channel_contexts if needed */
	if (!channel_contexts) {
		channel_contexts = g_hash_table_new_full(g_str_hash, g_str_equal,
		                                         g_free, (GDestroyNotify)free_channel_color_context);
	}
	
	/* Get or create channel context */
	ctx = g_hash_table_lookup(channel_contexts, channel_key);
	if (!ctx) {
		ctx = g_new0(channel_color_context, 1);
		ctx->channel_name = g_strdup(channel);
		ctx->server_tag = g_strdup(server_tag);
		ctx->nick_colors = g_hash_table_new_full(g_str_hash, g_str_equal,
		                                         g_free, (GDestroyNotify)free_nick_color_entry);
		g_hash_table_insert(channel_contexts, g_strdup(channel_key), ctx);
	}
	
	/* Get or create nick color entry */
	entry = g_hash_table_lookup(ctx->nick_colors, nick);
	if (!entry) {
		/* Nick not found - use hash color for consistency */
		entry = g_new0(nick_color_entry, 1);
		entry->nick = g_strdup(nick);
		
		/* Use hash-based color for new nicks */
		entry->color_index = hash_nick_to_color_index(nick, channel_key, palette_size);
		entry->last_seen = time(NULL);
		g_hash_table_insert(ctx->nick_colors, g_strdup(nick), entry);
	} else {
		entry->last_seen = time(NULL);
	}
	
	color_index = entry->color_index;
	g_free(channel_key);
	g_strfreev(palette);
	return color_index;
}

/* Main hash coloring expando - replaces nick with colored version */
static char *expando_nickcolored(SERVER_REC *server, void *item, int *free_ret)
{
	const char *display_nick;
	int palette_size;
	gchar **palette;
	int color_index;
	char *result;
	char *raw_format;
	
	if (!settings_get_bool("nick_hash_color_enabled") || !nick_context_valid || !current_nick) {
		return "";
	}
	
	/* Get nick to display - use nicktrunc if nick_column_enabled */
	if (settings_get_bool("nick_column_enabled")) {
		int temp_free = FALSE;
		display_nick = expando_nicktrunc(server, item, &temp_free);
		/* Handle memory correctly - always make our own copy */
		if (temp_free && display_nick) {
			/* nicktrunc allocated memory, take ownership */
			char *temp_nick = g_strdup(display_nick);
			g_free((char*)display_nick);
			display_nick = temp_nick;
		} else {
			/* nicktrunc didn't allocate, make our own copy */
			display_nick = display_nick ? g_strdup(display_nick) : g_strdup("");
		}
	} else {
		display_nick = g_strdup(current_nick ? current_nick : "");
	}
	
	/* Parse color palette */
	palette = parse_color_palette(settings_get_str("nick_hash_colors"), &palette_size);
	
	/* Get persistent color index for this nick */
	color_index = get_persistent_nick_color(current_nick, item, server);
	
	/* Return colored nick with proper formatting */
	*free_ret = TRUE;
	raw_format = g_strdup_printf("%%%s%%_%s%%_%%n", palette[color_index], display_nick);
	result = format_string_expand(raw_format, NULL);
	g_free(raw_format);
	
	g_strfreev(palette);
	g_free((char*)display_nick);
	return result;
}

/* Reset event parser and helper functions */

static gboolean should_reset_on_event(const char *event)
{
	const char *setting;
	
	setting = settings_get_str("nick_hash_reset_event");
	if (!setting || !*setting)
		setting = "quit";
	
	return strstr(setting, event) != NULL;
}

static void reset_nick_color(const char *server_tag, const char *channel, const char *nick)
{
	char *channel_key;
	channel_color_context *ctx;
	nick_color_entry *entry;
	int old_color, new_color, palette_size;
	gchar **palette;
	
	if (!channel_contexts)
		return;
		
	channel_key = g_strdup_printf("%s:%s", server_tag, channel);
	ctx = g_hash_table_lookup(channel_contexts, channel_key);
	
	if (!ctx || !ctx->nick_colors) {
		g_free(channel_key);
		return;
	}
	
	entry = g_hash_table_lookup(ctx->nick_colors, nick);
	if (!entry) {
		g_free(channel_key);
		return;
	}
	
	/* Get palette and generate new color different from current */
	palette = parse_color_palette(settings_get_str("nick_hash_colors"), &palette_size);
	old_color = entry->color_index;
	new_color = generate_random_color_index(old_color, palette_size);
	
	entry->color_index = new_color;
	entry->last_seen = time(NULL);
	
	g_strfreev(palette);
	g_free(channel_key);
}

static void reset_nick_on_server(const char *server_tag, const char *nick)
{
	GHashTableIter iter;
	gpointer key, value;
	channel_color_context *ctx;
	nick_color_entry *entry;
	char *server_prefix;
	int old_color, new_color, palette_size;
	gchar **palette;
	
	if (!channel_contexts)
		return;
		
	/* Get palette for color generation */
	palette = parse_color_palette(settings_get_str("nick_hash_colors"), &palette_size);
	server_prefix = g_strdup_printf("%s:", server_tag);
	
	
	g_hash_table_iter_init(&iter, channel_contexts);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (g_str_has_prefix((char*)key, server_prefix)) {
			ctx = (channel_color_context*)value;
			if (ctx && ctx->nick_colors) {
				entry = g_hash_table_lookup(ctx->nick_colors, nick);
				if (entry) {
					old_color = entry->color_index;
					new_color = generate_random_color_index(old_color, palette_size);
					entry->color_index = new_color;
					entry->last_seen = time(NULL);
				}
			}
		}
	}
	
	g_strfreev(palette);
	g_free(server_prefix);
}


/* Signal handlers for cleanup events */

static void cleanup_nick_on_quit(SERVER_REC *server, const char *nick, const char *address, const char *reason)
{
	if (!should_reset_on_event("quit") || !server || !nick)
		return;
		
	reset_nick_on_server(server->tag, nick);
}

static void cleanup_nick_on_part(SERVER_REC *server, const char *channel, const char *nick, const char *address, const char *reason)
{
	if (!should_reset_on_event("part") || !server || !channel || !nick)
		return;
		
	reset_nick_color(server->tag, channel, nick);
}

static void cleanup_nick_on_nickchange(SERVER_REC *server, const char *new_nick, const char *old_nick, const char *address)
{
	if (!should_reset_on_event("nickchange") || !server || !old_nick)
		return;
		
	reset_nick_on_server(server->tag, old_nick);
}

/* /nickhash command handler */

static void cmd_nickhash(const char *data, SERVER_REC *server, WI_ITEM_REC *item)
{
	char **params;
	char *subcmd, *channel, *nick;
	
	if (!data || !*data) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Usage: /nickhash shift <#channel> <nick>");
		return;
	}
	
	params = g_strsplit(data, " ", 3);
	if (!params[0]) {
		printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Usage: /nickhash shift <#channel> <nick>");
		g_strfreev(params);
		return;
	}
	
	subcmd = g_strstrip(params[0]);
	
	if (g_strcmp0(subcmd, "shift") == 0) {
		if (!params[1] || !params[2]) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Usage: /nickhash shift <#channel> <nick>");
			g_strfreev(params);
			return;
		}
		
		channel = g_strstrip(params[1]);
		nick = g_strstrip(params[2]);
		
		if (!server) {
			printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Not connected to server");
			g_strfreev(params);
			return;
		}
		
		/* DON'T remove # prefix - keep it consistent with get_persistent_nick_color */
			
		reset_nick_color(server->tag, channel, nick);
		printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Shifted color for nick %s in %s", nick, channel);
		
	} else {
		printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Unknown subcommand: %s", subcmd);
	}
	
	g_strfreev(params);
}

void fe_expandos_init(void)
{
	expando_create("winref", expando_winref, "window changed", EXPANDO_ARG_NONE,
	               "window refnum changed", EXPANDO_ARG_WINDOW, NULL);
	expando_create("winname", expando_winname, "window changed", EXPANDO_ARG_NONE,
	               "window name changed", EXPANDO_ARG_WINDOW, NULL);
	expando_create("nickalign", expando_nickalign, "message public", EXPANDO_ARG_NONE,
	               "message own_public", EXPANDO_ARG_NONE, NULL);
	expando_create("nicktrunc", expando_nicktrunc, "message public", EXPANDO_ARG_NONE,
	               "message own_public", EXPANDO_ARG_NONE, NULL);
	expando_create("nickcolored", expando_nickcolored, "message public", EXPANDO_ARG_NONE,
	               "message own_public", EXPANDO_ARG_NONE, NULL);
	
	/* Register signal handlers for nick cleanup */
	signal_add("message quit", (SIGNAL_FUNC) cleanup_nick_on_quit);
	signal_add("message part", (SIGNAL_FUNC) cleanup_nick_on_part);
	signal_add("message nick", (SIGNAL_FUNC) cleanup_nick_on_nickchange);
	
	/* Register command */
	command_bind("nickhash", NULL, (SIGNAL_FUNC) cmd_nickhash);
}

void fe_expandos_deinit(void)
{
	expando_destroy("winref", expando_winref);
	expando_destroy("winname", expando_winname);
	expando_destroy("nickalign", expando_nickalign);
	expando_destroy("nicktrunc", expando_nicktrunc);
	expando_destroy("nickcolored", expando_nickcolored);
	
	/* Unregister signal handlers */
	signal_remove("message quit", (SIGNAL_FUNC) cleanup_nick_on_quit);
	signal_remove("message part", (SIGNAL_FUNC) cleanup_nick_on_part);
	signal_remove("message nick", (SIGNAL_FUNC) cleanup_nick_on_nickchange);
	
	/* Unregister command */
	command_unbind("nickhash", (SIGNAL_FUNC) cmd_nickhash);
	
	/* Clean up hash coloring data */
	if (channel_contexts) {
		g_hash_table_destroy(channel_contexts);
		channel_contexts = NULL;
	}
}
