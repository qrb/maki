/*
 * Copyright (c) 2008 Michael Kuhn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "maki.h"

/**
 * This function is a wrapper around sashimi_connect().
 * It handles the initial login with NICK and USER and emits the connect signal.
 */
gint maki_connect (struct maki_connection* m_conn)
{
	gint ret;

	if ((ret = sashimi_connect(m_conn->connection)) == 0)
	{
		gchar* buffer;
		GTimeVal time;

		m_conn->connected = TRUE;
		m_conn->reconnect = TRUE;
		m_conn->retries = m_conn->maki->config.reconnect.retries;

		g_free(m_conn->nick);
		m_conn->nick = g_strdup(m_conn->initial_nick);

		buffer = g_strdup_printf("NICK %s", m_conn->initial_nick);
		sashimi_send(m_conn->connection, buffer);
		g_free(buffer);

		buffer = g_strdup_printf("USER %s 0 * :%s", m_conn->initial_nick, m_conn->name);
		sashimi_send(m_conn->connection, buffer);
		g_free(buffer);

		g_get_current_time(&time);
		maki_dbus_emit_connect(m_conn->maki->bus, time.tv_sec, m_conn->server);
	}

	return ret;
}

/**
 * This function is a wrapper around sashimi_disconnect().
 */
gint maki_disconnect (struct maki_connection* m_conn)
{
	gint ret;

	m_conn->connected = FALSE;
	ret = sashimi_disconnect(m_conn->connection);

	return ret;
}

/**
 * This function gets called by the quit method after a delay.
 */
gboolean maki_disconnect_timeout (gpointer data)
{
	struct maki_connection* m_conn = data;

	g_hash_table_remove(m_conn->maki->connections, m_conn->server);

	return FALSE;
}

/**
 * This function gets called when a user is removed from the users hash table.
 */
void maki_user_destroy (gpointer data)
{
	struct maki_user* m_user = data;

	g_free(m_user->nick);
	g_free(m_user);
}

/**
 * This function gets called when a channel is removed from the channels hash table.
 */
void maki_channel_destroy (gpointer data)
{
	struct maki_channel* m_chan = data;

	g_hash_table_destroy(m_chan->users);
	g_free(m_chan->key);
	g_free(m_chan->name);
	g_free(m_chan);
}

/**
 * This function gets called when a connection is removed from the connections hash table.
 */
void maki_connection_destroy (gpointer data)
{
	struct maki_connection* m_conn = data;

	g_hash_table_destroy(m_conn->channels);
	maki_disconnect(m_conn);
	sashimi_free(m_conn->connection);
	g_free(m_conn->name);
	g_free(m_conn->nick);
	g_free(m_conn->initial_nick);
	g_free(m_conn->server);
	g_free(m_conn);
}

/**
 * A convenience function to remove a colon before an argument.
 * It also checks for NULL.
 */
gchar* maki_remove_colon (gchar* string)
{
	if (string != NULL && string[0] == ':')
	{
		++string;
	}

	return string;
}