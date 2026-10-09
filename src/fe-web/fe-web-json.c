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
#include <errno.h>
#include <limits.h>

/* Value of the 4 hex digits at p, or -1 */
static int json_hex4(const char *p)
{
	int i, value = 0;

	for (i = 0; i < 4; i++) {
		int digit = g_ascii_xdigit_value(p[i]);

		if (digit < 0)
			return -1;
		value = value * 16 + digit;
	}
	return value;
}

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
				/* \uXXXX is a UTF-16 unit: a surrogate pair is one
				 * character, and a lone surrogate or a NUL (which a
				 * C string cannot hold) becomes U+FFFD */
				int code = json_hex4(p + 1);

				if (code < 0) {
					/* not 4 hex digits: keep it as it is */
					g_string_append_c(result, 'u');
					break;
				}
				p += 4;
				if (code >= 0xD800 && code <= 0xDBFF && p[1] == '\\' && p[2] == 'u') {
					int low = json_hex4(p + 3);

					if (low >= 0xDC00 && low <= 0xDFFF) {
						code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						p += 6;
					}
				}
				if (code == 0 || (code >= 0xD800 && code <= 0xDFFF))
					code = 0xFFFD;
				g_string_append_unichar(result, code);
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
	while (*pos != '\0' && isspace((unsigned char) *pos)) {
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
	char *end;
	long value;

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
	while (*pos != '\0' && isspace((unsigned char) *pos)) {
		pos++;
	}

	/* JSON booleans, as server_list sends them (use_tls, autoconnect) */
	if (strncmp(pos, "true", 4) == 0)
		return 1;
	if (strncmp(pos, "false", 5) == 0)
		return 0;

	/* Parse integer: one that does not fit an int is not taken
	 * (sscanf("%d") is undefined for it) */
	errno = 0;
	value = strtol(pos, &end, 10);
	if (end == pos || errno == ERANGE || value < INT_MIN || value > INT_MAX)
		return default_value;
	return (int) value;
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

/* Append "key":"value", (escaped) or "key":null, */
static void json_append_string(GString *json, const char *key, const char *value)
{
	char *escaped;

	if (value == NULL) {
		g_string_append_printf(json, "\"%s\":null,", key);
		return;
	}
	escaped = fe_web_escape_json(value);
	g_string_append_printf(json, "\"%s\":\"%s\",", key, escaped);
	g_free(escaped);
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
	json_append_string(json, "name", rec->name != NULL ? rec->name : "");
	g_string_append(json, "\"chat_type\":\"IRC\",");

	/* Optional fields */
	json_append_string(json, "nick", rec->nick);
	json_append_string(json, "alternate_nick", rec->alternate_nick);
	json_append_string(json, "username", rec->username);
	json_append_string(json, "realname", rec->realname);
	json_append_string(json, "own_host", rec->own_host);
	json_append_string(json, "autosendcmd", rec->autosendcmd);
	json_append_string(json, "usermode", rec->usermode);

	/* SASL fields */
	json_append_string(json, "sasl_mechanism", rec->sasl_mechanism);
	json_append_string(json, "sasl_username", rec->sasl_username);

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
	json_append_string(json, "address", rec->address != NULL ? rec->address : "");
	g_string_append_printf(json, "\"port\":%d,", rec->port);

	/* Optional fields */
	json_append_string(json, "chatnet", rec->chatnet);

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
	json_append_string(json, "tls_cert", rec->tls_cert);
	json_append_string(json, "tls_pkey", rec->tls_pkey);

	/* Mask TLS password */
	if (rec->tls_pass != NULL) {
		g_string_append(json, "\"tls_pass\":\"***\",");
	} else {
		g_string_append(json, "\"tls_pass\":null,");
	}

	json_append_string(json, "tls_cafile", rec->tls_cafile);
	json_append_string(json, "tls_capath", rec->tls_capath);
	json_append_string(json, "tls_ciphers", rec->tls_ciphers);
	json_append_string(json, "tls_pinned_cert", rec->tls_pinned_cert);
	json_append_string(json, "tls_pinned_pubkey", rec->tls_pinned_pubkey);

	/* Network binding */
	json_append_string(json, "own_host", rec->own_host);

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
		char *escaped_code = fe_web_escape_json(error_code);
		g_string_append_printf(json, "\"error_code\":\"%s\"", escaped_code);
		g_free(escaped_code);
	} else {
		g_string_append(json, "\"error_code\":null");
	}

	g_string_append(json, "}");

	return json;
}
