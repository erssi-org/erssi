/*
 net-nonblock.c : Nonblocking net_connect()

    Copyright (C) 1998-2000 Timo Sirainen

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
*/

#include "module.h"

#include <signal.h>

#include <irssi/src/core/network.h>
#include <irssi/src/core/net-nonblock.h>
#ifdef HAVE_CAPSICUM
#include <irssi/src/core/capsicum.h>
#endif

typedef struct {
	GResolverNameLookupFlags flags;
	NetGethostbynameContinuationFunc cont;
	void *cont_data;
} NET_GETHOSTBYNAME_CALLBACK_DATA;

static void net_gethostbyname_callback(GResolver *resolver, GAsyncResult *result,
                                       NET_GETHOSTBYNAME_CALLBACK_DATA *data)
{
	/* GList<GInetAddress> */
	GList *ailist;
	GError *error;
	RESOLVED_IP_REC *iprec;

	error = NULL;
#if GLIB_CHECK_VERSION(2, 59, 0)
	ailist = g_resolver_lookup_by_name_with_flags_finish(resolver, result, &error);
#else
	/* compatibility code for old GLib */
	ailist = g_resolver_lookup_by_name_finish(resolver, result, &error);
	if (error == NULL && data->flags) {
		GList *ll, *lll;

		for (ll = ailist; ll != NULL; ll = lll) {
			GInetAddress *address;
			GSocketFamily family;

			address = G_INET_ADDRESS(ll->data);
			family = g_inet_address_get_family(address);
			lll = ll->next;

			if ((data->flags == G_RESOLVER_NAME_LOOKUP_FLAGS_IPV4_ONLY &&
			     family == G_SOCKET_FAMILY_IPV6) ||
			    (data->flags == G_RESOLVER_NAME_LOOKUP_FLAGS_IPV6_ONLY &&
			     family == G_SOCKET_FAMILY_IPV4)) {
				g_object_unref(address);
				ailist = g_list_delete_link(ailist, ll);
			}
		}

		if (ailist == NULL) {
			g_set_error(&error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND,
			            data->flags == G_RESOLVER_NAME_LOOKUP_FLAGS_IPV4_ONLY ?
			                "IPv4 address not found for host" :
			                "IPv6 address not found for host");
		}
	}
#endif
	iprec = g_new0(RESOLVED_IP_REC, 1);
	if (error != NULL) {
		iprec->error = error;
	} else {
		iprec->ailist = ailist;
	}
	g_object_unref(resolver);
	resolved_ip_ref(iprec);

	data->cont(iprec, data->cont_data);
	g_free(data);
}

#ifdef HAVE_CAPSICUM
typedef struct {
	char *addr;
	GResolverNameLookupFlags flags;
	GCancellable *cancellable;
	NetGethostbynameContinuationFunc cont;
	void *cont_data;
} CAPSICUM_LOOKUP_REC;

static GInetAddress *ipaddr_to_inet(const IPADDR *ip)
{
	return g_inet_address_new_from_bytes((const guint8 *) &ip->ip,
	                                     ip->family == AF_INET ? G_SOCKET_FAMILY_IPV4 :
	                                                             G_SOCKET_FAMILY_IPV6);
}

/* In capability mode GResolver cannot reach DNS (no sockets, no
   resolv.conf): the lookup goes to the helper process outside the
   sandbox, as irssi 1.4 did. The helper answers at once; the result is
   handed over from the main loop, like GResolver's. */
static gboolean capsicum_lookup_done(CAPSICUM_LOOKUP_REC *rec)
{
	RESOLVED_IP_REC *iprec;
	IPADDR ip4, ip6;

	iprec = g_new0(RESOLVED_IP_REC, 1);
	memset(&ip4, 0, sizeof(ip4));
	memset(&ip6, 0, sizeof(ip6));
	if (g_cancellable_is_cancelled(rec->cancellable)) {
		g_set_error(&iprec->error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
		            "Operation was cancelled");
	} else if (capsicum_net_gethostbyname(rec->addr, &ip4, &ip6) != 0 ||
	           (ip4.family == 0 && ip6.family == 0)) {
		g_set_error(&iprec->error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND,
		            "Error resolving \"%s\"", rec->addr);
	} else {
		if (ip6.family != 0 && rec->flags != G_RESOLVER_NAME_LOOKUP_FLAGS_IPV4_ONLY)
			iprec->ailist = g_list_append(iprec->ailist, ipaddr_to_inet(&ip6));
		if (ip4.family != 0 && rec->flags != G_RESOLVER_NAME_LOOKUP_FLAGS_IPV6_ONLY)
			iprec->ailist = g_list_append(iprec->ailist, ipaddr_to_inet(&ip4));
		if (iprec->ailist == NULL)
			g_set_error(&iprec->error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND,
			            rec->flags == G_RESOLVER_NAME_LOOKUP_FLAGS_IPV4_ONLY ?
			                "IPv4 address not found for host" :
			                "IPv6 address not found for host");
	}
	resolved_ip_ref(iprec);
	rec->cont(iprec, rec->cont_data);

	g_object_unref(rec->cancellable);
	g_free(rec->addr);
	g_free(rec);
	return FALSE;
}
#endif

/* nonblocking gethostbyname() */
GCancellable *net_gethostbyname_nonblock(const char *addr, GResolverNameLookupFlags flags,
                                         NetGethostbynameContinuationFunc cont, void *cont_data)
{
	GResolver *resolver;
	GCancellable *cancellable;
	NET_GETHOSTBYNAME_CALLBACK_DATA *data;

	g_return_val_if_fail(addr != NULL, FALSE);

	cancellable = g_cancellable_new();
#ifdef HAVE_CAPSICUM
	if (capsicum_enabled()) {
		CAPSICUM_LOOKUP_REC *rec = g_new0(CAPSICUM_LOOKUP_REC, 1);

		rec->addr = g_strdup(addr);
		rec->flags = flags;
		rec->cancellable = g_object_ref(cancellable);
		rec->cont = cont;
		rec->cont_data = cont_data;
		g_idle_add((GSourceFunc) capsicum_lookup_done, rec);
		return cancellable;
	}
#endif
	resolver = g_resolver_get_default();
	data = g_new0(NET_GETHOSTBYNAME_CALLBACK_DATA, 1);
	data->flags = flags;
	data->cont = cont;
	data->cont_data = cont_data;
#if GLIB_CHECK_VERSION(2, 59, 0)
	g_resolver_lookup_by_name_with_flags_async(resolver, addr, flags, cancellable,
	                                           (GAsyncReadyCallback) net_gethostbyname_callback,
	                                           data);
#else
	/* compatibility code for old GLib */
	g_resolver_lookup_by_name_async(resolver, addr, cancellable,
	                                (GAsyncReadyCallback) net_gethostbyname_callback, data);
#endif
	return cancellable;
}
