/*
 * Copyright (c) 2008-2009 Michael Kuhn
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

enum
{
	s_read,
	s_write,
	s_num
};

struct maki_dcc_send_in
{
	GIOChannel* channel;
	gchar* path;
	goffset position;
	goffset size;
	guint sources[s_num];

	gchar* address;
	guint16 port;
};

static gboolean maki_dcc_send_in_read (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gchar buffer[1024];
	gsize bytes_read;
	GIOStatus status;
	struct maki_dcc_send_in* dcc_in = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto finish;
	}

	while ((status = g_io_channel_read_chars(source, buffer, 1024, &bytes_read, NULL)) == G_IO_STATUS_NORMAL)
	{
		guint32 pos;

		dcc_in->position += bytes_read;
		pos = htonl(dcc_in->position);

		g_io_channel_write_chars(dcc_in->channel, buffer, bytes_read, NULL, NULL);
		g_io_channel_flush(dcc_in->channel, NULL);

		g_io_channel_write_chars(source, (gchar*)&pos, sizeof(pos), NULL, NULL);
		g_io_channel_flush(source, NULL);

		if (dcc_in->size > 0 && dcc_in->position >= dcc_in->size)
		{
			goto finish;
		}
	}

	if (status == G_IO_STATUS_EOF)
	{
		goto finish;
	}

	return TRUE;

finish:
	maki_dcc_send_in_free(dcc_in);

	g_io_channel_shutdown(source, FALSE, NULL);
	g_io_channel_unref(source);

	return FALSE;
}

static gboolean maki_dcc_send_in_write (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gint val;
	gchar* dirname;
	socklen_t len = sizeof(val);
	makiDCCSendIn* dcc_in = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto error;
	}

	if (getsockopt(g_io_channel_unix_get_fd(source), SOL_SOCKET, SO_ERROR, &val, &len) == -1
	    || val != 0)
	{
		goto error;
	}

	if (g_file_test(dcc_in->path, G_FILE_TEST_EXISTS))
	{
		goto error;
	}

	dirname = g_path_get_dirname(dcc_in->path);
	g_mkdir_with_parents(dirname, 0777);
	g_free(dirname);

	if ((dcc_in->channel = g_io_channel_new_file(dcc_in->path, "w", NULL)) == NULL)
	{
		goto error;
	}

	g_io_channel_set_close_on_unref(dcc_in->channel, TRUE);
	g_io_channel_set_encoding(dcc_in->channel, NULL, NULL);

	dcc_in->sources[s_write] = 0;
	dcc_in->sources[s_read] = g_io_add_watch(source, G_IO_IN | G_IO_HUP | G_IO_ERR, maki_dcc_send_in_read, dcc_in);

	return FALSE;

error:
	maki_dcc_send_in_free(dcc_in);

	g_io_channel_shutdown(source, FALSE, NULL);
	g_io_channel_unref(source);

	return FALSE;
}

gchar* maki_dcc_send_get_file_name (const gchar* string, gsize* length)
{
	gchar* file_name;
	gchar* tmp;
	gsize len = 0;

	tmp = NULL;

	if (string[len] == '"')
	{
		len++;

		while (string[len] != '\0' && string[len] != '"')
		{
			len++;
		}

		if (string[len] != '\0')
		{
			len++;
		}

		if (len - 2 > 0)
		{
			tmp = g_strndup(string + 1, len - 2);
		}
	}
	else
	{
		while (string[len] != '\0' && string[len] != ' ')
		{
			len++;
		}

		if (len > 0)
		{
			tmp = g_strndup(string, len);
		}
	}

	if (string[len] == '\0' || tmp == NULL)
	{
		g_free(tmp);
		return NULL;
	}

	file_name = g_path_get_basename(tmp);
	g_free(tmp);

	*length = len;

	return file_name;
}

makiDCCSendIn* maki_dcc_send_in_new (makiServer* serv, const gchar* dir_name, const gchar* file_name, guint32 address, guint16 port, goffset file_size, const gchar* token)
{
	gchar* downloads_dir;
	makiDCCSendIn* dcc_in;

	downloads_dir = maki_instance_config_get_string(serv->instance, "directories", "downloads");

	dcc_in = g_new(makiDCCSendIn, 1);

	dcc_in->channel = NULL;
	dcc_in->path = g_build_filename(downloads_dir, dir_name, file_name, NULL);
	dcc_in->position = 0;
	dcc_in->size = file_size;

	dcc_in->sources[s_read] = 0;
	dcc_in->sources[s_write] = 0;

	dcc_in->address = g_strdup_printf("%u.%u.%u.%u", (address & 0xff000000) >> 24, (address & 0x00ff0000) >> 16, (address & 0x0000ff00) >> 8, (address & 0x000000ff) >> 0);
	dcc_in->port = port;

	g_free(downloads_dir);

	return dcc_in;
}

void maki_dcc_send_in_free (makiDCCSendIn* dcc_in)
{
	if (dcc_in->channel != NULL)
	{
		g_io_channel_shutdown(dcc_in->channel, FALSE, NULL);
		g_io_channel_unref(dcc_in->channel);
	}

	g_free(dcc_in->address);
	g_free(dcc_in->path);
	g_free(dcc_in);
}

gboolean maki_dcc_send_in_accept (makiDCCSendIn* dcc_in)
{
	GIOChannel* channel;

	g_return_val_if_fail(dcc_in != NULL, FALSE);

	if ((channel = i_io_channel_unix_new_address(dcc_in->address, dcc_in->port, TRUE)) == NULL)
	{
		return FALSE;
	}

	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);

	dcc_in->sources[s_write] = g_io_add_watch(channel, G_IO_OUT | G_IO_HUP | G_IO_ERR, maki_dcc_send_in_write, dcc_in);

	return TRUE;
}
