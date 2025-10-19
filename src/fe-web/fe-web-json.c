/*
 fe-web-json.c : Simple JSON parser for fe-web

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "fe-web.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Helper: Unescape JSON string (\\, \", \n, \r, \t, \b, \f, \uXXXX) */
static char *fe_web_json_unescape(const char *str)
{
	GString *result;
	const char *p;

	if (str == NULL) {
		return NULL;
	}

	result = g_string_new("");
	p = str;

	while (*p != '\0') {
		if (*p == '\\' && *(p + 1) != '\0') {
			p++; /* Skip backslash */
			switch (*p) {
			case '"':
				g_string_append_c(result, '"');
				break;
			case '\\':
				g_string_append_c(result, '\\');
				break;
			case '/':
				g_string_append_c(result, '/');
				break;
			case 'b':
				g_string_append_c(result, '\b');
				break;
			case 'f':
				g_string_append_c(result, '\f');
				break;
			case 'n':
				g_string_append_c(result, '\n');
				break;
			case 'r':
				g_string_append_c(result, '\r');
				break;
			case 't':
				g_string_append_c(result, '\t');
				break;
			case 'u': {
				/* Unicode escape: \uXXXX */
				char hex[5] = { 0 };
				unsigned int code;
				int i;

				/* Read 4 hex digits */
				for (i = 0; i < 4 && *(p + 1 + i) != '\0'; i++) {
					hex[i] = *(p + 1 + i);
				}

				if (i == 4) {
					code = (unsigned int) strtol(hex, NULL, 16);
					/* Simple UTF-8 encoding (BMP only) */
					if (code < 0x80) {
						g_string_append_c(result, (char) code);
					} else if (code < 0x800) {
						g_string_append_c(result,
						                  (char) (0xC0 | (code >> 6)));
						g_string_append_c(result,
						                  (char) (0x80 | (code & 0x3F)));
					} else {
						g_string_append_c(result,
						                  (char) (0xE0 | (code >> 12)));
						g_string_append_c(
						    result, (char) (0x80 | ((code >> 6) & 0x3F)));
						g_string_append_c(result,
						                  (char) (0x80 | (code & 0x3F)));
					}
					p += 4; /* Skip the 4 hex digits */
				}
				break;
			}
			default:
				/* Unknown escape - keep as-is */
				g_string_append_c(result, *p);
				break;
			}
			p++;
		} else {
			g_string_append_c(result, *p);
			p++;
		}
	}

	return g_string_free(result, FALSE);
}

/* Simple JSON value extraction - finds "key":"value" or "key":123 */
char *fe_web_json_get_string(const char *json, const char *key)
{
	char *search_str;
	char *pos;
	char *start;
	char *end;
	char *result;

	if (json == NULL || key == NULL) {
		return NULL;
	}

	/* Build search string: "key": */
	search_str = g_strdup_printf("\"%s\"", key);
	pos = strstr(json, search_str);
	g_free(search_str);

	if (pos == NULL) {
		return NULL;
	}

	/* Skip past the key and colon */
	pos = strchr(pos + strlen(key), ':');
	if (pos == NULL) {
		return NULL;
	}
	pos++;

	/* Skip whitespace */
	while (*pos != '\0' && isspace(*pos)) {
		pos++;
	}

	/* Check if it's a string (starts with ") */
	if (*pos != '"') {
		return NULL;
	}

	start = pos + 1;

	/* Find end of string (handle escaped quotes) */
	end = start;
	while (*end != '\0') {
		if (*end == '\\' && *(end + 1) != '\0') {
			end += 2;
			continue;
		}
		if (*end == '"') {
			break;
		}
		end++;
	}

	if (*end != '"') {
		return NULL;
	}

	/* Extract string and unescape JSON escape sequences */
	{
		char *escaped_str = g_strndup(start, end - start);
		result = fe_web_json_unescape(escaped_str);
		g_free(escaped_str);
	}

	return result;
}

/* Get integer value from JSON */
int fe_web_json_get_int(const char *json, const char *key, int default_value)
{
	char *search_str;
	char *pos;
	int value;

	if (json == NULL || key == NULL) {
		return default_value;
	}

	/* Build search string: "key": */
	search_str = g_strdup_printf("\"%s\"", key);
	pos = strstr(json, search_str);
	g_free(search_str);

	if (pos == NULL) {
		return default_value;
	}

	/* Skip past the key and colon */
	pos = strchr(pos + strlen(key), ':');
	if (pos == NULL) {
		return default_value;
	}
	pos++;

	/* Skip whitespace */
	while (*pos != '\0' && isspace(*pos)) {
		pos++;
	}

	/* Parse integer */
	if (sscanf(pos, "%d", &value) == 1) {
		return value;
	}

	return default_value;
}

/* Check if JSON has a key */
int fe_web_json_has_key(const char *json, const char *key)
{
	char *search_str;
	char *pos;

	if (json == NULL || key == NULL) {
		return 0;
	}

	search_str = g_strdup_printf("\"%s\"", key);
	pos = strstr(json, search_str);
	g_free(search_str);

	return pos != NULL;
}

/* Build JSON string for a network (IRC_CHATNET_REC) */
GString *fe_web_build_network_json(IRC_CHATNET_REC *rec)
{
	GString *json;

	if (rec == NULL) {
		return NULL;
	}

	json = g_string_new("{");

	/* Required fields */
	g_string_append_printf(json, "\"name\":\"%s\",", fe_web_escape_json(rec->name));
	g_string_append(json, "\"chat_type\":\"IRC\",");

	/* Optional fields */
	if (rec->nick != NULL) {
		g_string_append_printf(json, "\"nick\":\"%s\",", fe_web_escape_json(rec->nick));
	} else {
		g_string_append(json, "\"nick\":null,");
	}

	if (rec->alternate_nick != NULL) {
		g_string_append_printf(json, "\"alternate_nick\":\"%s\",",
		                       fe_web_escape_json(rec->alternate_nick));
	} else {
		g_string_append(json, "\"alternate_nick\":null,");
	}

	if (rec->username != NULL) {
		g_string_append_printf(json, "\"username\":\"%s\",",
		                       fe_web_escape_json(rec->username));
	} else {
		g_string_append(json, "\"username\":null,");
	}

	if (rec->realname != NULL) {
		g_string_append_printf(json, "\"realname\":\"%s\",",
		                       fe_web_escape_json(rec->realname));
	} else {
		g_string_append(json, "\"realname\":null,");
	}

	if (rec->own_host != NULL) {
		g_string_append_printf(json, "\"own_host\":\"%s\",",
		                       fe_web_escape_json(rec->own_host));
	} else {
		g_string_append(json, "\"own_host\":null,");
	}

	if (rec->autosendcmd != NULL) {
		g_string_append_printf(json, "\"autosendcmd\":\"%s\",",
		                       fe_web_escape_json(rec->autosendcmd));
	} else {
		g_string_append(json, "\"autosendcmd\":null,");
	}

	if (rec->usermode != NULL) {
		g_string_append_printf(json, "\"usermode\":\"%s\",",
		                       fe_web_escape_json(rec->usermode));
	} else {
		g_string_append(json, "\"usermode\":null,");
	}

	/* SASL fields */
	if (rec->sasl_mechanism != NULL) {
		g_string_append_printf(json, "\"sasl_mechanism\":\"%s\",",
		                       fe_web_escape_json(rec->sasl_mechanism));
	} else {
		g_string_append(json, "\"sasl_mechanism\":null,");
	}

	if (rec->sasl_username != NULL) {
		g_string_append_printf(json, "\"sasl_username\":\"%s\",",
		                       fe_web_escape_json(rec->sasl_username));
	} else {
		g_string_append(json, "\"sasl_username\":null,");
	}

	/* Mask password - never send actual password */
	if (rec->sasl_password != NULL) {
		g_string_append(json, "\"sasl_password\":\"***\",");
	} else {
		g_string_append(json, "\"sasl_password\":null,");
	}

	/* Numeric settings */
	g_string_append_printf(json, "\"max_kicks\":%d,", rec->max_kicks);
	g_string_append_printf(json, "\"max_msgs\":%d,", rec->max_msgs);
	g_string_append_printf(json, "\"max_modes\":%d,", rec->max_modes);
	g_string_append_printf(json, "\"max_whois\":%d,", rec->max_whois);
	g_string_append_printf(json, "\"max_cmds_at_once\":%d,", rec->max_cmds_at_once);
	g_string_append_printf(json, "\"cmd_queue_speed\":%d,", rec->cmd_queue_speed);
	g_string_append_printf(json, "\"max_query_chans\":%d", rec->max_query_chans);

	g_string_append(json, "}");

	return json;
}

/* Build JSON string for a server (IRC_SERVER_SETUP_REC) */
GString *fe_web_build_server_json(IRC_SERVER_SETUP_REC *rec)
{
	GString *json;

	if (rec == NULL) {
		return NULL;
	}

	json = g_string_new("{");

	/* Required fields */
	g_string_append_printf(json, "\"address\":\"%s\",", fe_web_escape_json(rec->address));
	g_string_append_printf(json, "\"port\":%d,", rec->port);

	/* Optional fields */
	if (rec->chatnet != NULL) {
		g_string_append_printf(json, "\"chatnet\":\"%s\",",
		                       fe_web_escape_json(rec->chatnet));
	} else {
		g_string_append(json, "\"chatnet\":null,");
	}

	/* Password - never send actual password */
	if (rec->password != NULL) {
		g_string_append(json, "\"password\":\"***\",");
	} else {
		g_string_append(json, "\"password\":null,");
	}

	/* Boolean flags */
	g_string_append_printf(json, "\"autoconnect\":%s,", rec->autoconnect ? "true" : "false");
	g_string_append_printf(json, "\"use_tls\":%s,", rec->use_tls ? "true" : "false");
	g_string_append_printf(json, "\"tls_verify\":%s,", rec->tls_verify ? "true" : "false");

	/* TLS certificate fields */
	if (rec->tls_cert != NULL) {
		g_string_append_printf(json, "\"tls_cert\":\"%s\",",
		                       fe_web_escape_json(rec->tls_cert));
	} else {
		g_string_append(json, "\"tls_cert\":null,");
	}

	if (rec->tls_pkey != NULL) {
		g_string_append_printf(json, "\"tls_pkey\":\"%s\",",
		                       fe_web_escape_json(rec->tls_pkey));
	} else {
		g_string_append(json, "\"tls_pkey\":null,");
	}

	/* Mask TLS password */
	if (rec->tls_pass != NULL) {
		g_string_append(json, "\"tls_pass\":\"***\",");
	} else {
		g_string_append(json, "\"tls_pass\":null,");
	}

	if (rec->tls_cafile != NULL) {
		g_string_append_printf(json, "\"tls_cafile\":\"%s\",",
		                       fe_web_escape_json(rec->tls_cafile));
	} else {
		g_string_append(json, "\"tls_cafile\":null,");
	}

	if (rec->tls_capath != NULL) {
		g_string_append_printf(json, "\"tls_capath\":\"%s\",",
		                       fe_web_escape_json(rec->tls_capath));
	} else {
		g_string_append(json, "\"tls_capath\":null,");
	}

	if (rec->tls_ciphers != NULL) {
		g_string_append_printf(json, "\"tls_ciphers\":\"%s\",",
		                       fe_web_escape_json(rec->tls_ciphers));
	} else {
		g_string_append(json, "\"tls_ciphers\":null,");
	}

	if (rec->tls_pinned_cert != NULL) {
		g_string_append_printf(json, "\"tls_pinned_cert\":\"%s\",",
		                       fe_web_escape_json(rec->tls_pinned_cert));
	} else {
		g_string_append(json, "\"tls_pinned_cert\":null,");
	}

	if (rec->tls_pinned_pubkey != NULL) {
		g_string_append_printf(json, "\"tls_pinned_pubkey\":\"%s\",",
		                       fe_web_escape_json(rec->tls_pinned_pubkey));
	} else {
		g_string_append(json, "\"tls_pinned_pubkey\":null,");
	}

	/* Network binding */
	if (rec->own_host != NULL) {
		g_string_append_printf(json, "\"own_host\":\"%s\",",
		                       fe_web_escape_json(rec->own_host));
	} else {
		g_string_append(json, "\"own_host\":null,");
	}

	/* Numeric settings */
	g_string_append_printf(json, "\"family\":%d,", rec->family);
	g_string_append_printf(json, "\"max_cmds_at_once\":%d,", rec->max_cmds_at_once);
	g_string_append_printf(json, "\"cmd_queue_speed\":%d,", rec->cmd_queue_speed);
	g_string_append_printf(json, "\"max_query_chans\":%d,", rec->max_query_chans);
	g_string_append_printf(json, "\"starttls\":%d,", rec->starttls);

	/* More boolean flags */
	g_string_append_printf(json, "\"no_cap\":%s,", rec->no_cap ? "true" : "false");
	g_string_append_printf(json, "\"no_proxy\":%s,", rec->no_proxy ? "true" : "false");
	g_string_append_printf(json, "\"last_failed\":%s,", rec->last_failed ? "true" : "false");
	g_string_append_printf(json, "\"banned\":%s,", rec->banned ? "true" : "false");
	g_string_append_printf(json, "\"dns_error\":%s", rec->dns_error ? "true" : "false");

	g_string_append(json, "}");

	return json;
}

/* Build command result JSON */
GString *fe_web_build_command_result_json(gboolean success, const char *message,
                                          const char *error_code)
{
	GString *json;
	char *escaped_msg;

	json = g_string_new("{");
	g_string_append_printf(json, "\"success\":%s,", success ? "true" : "false");

	if (message != NULL) {
		escaped_msg = fe_web_escape_json(message);
		g_string_append_printf(json, "\"message\":\"%s\",", escaped_msg);
		g_free(escaped_msg);
	} else {
		g_string_append(json, "\"message\":null,");
	}

	if (error_code != NULL) {
		g_string_append_printf(json, "\"error_code\":\"%s\"",
		                       fe_web_escape_json(error_code));
	} else {
		g_string_append(json, "\"error_code\":null");
	}

	g_string_append(json, "}");

	return json;
}
