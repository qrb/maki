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
	s_in_read,
	s_in_write,
	s_in_num
};

enum
{
	s_out_listen,
	s_out_read,
	s_out_write,
	s_out_num
};

struct maki_dcc_send
{
	GIOChannel* channel;
	gchar* path;

	goffset position;
	goffset size;

	guint32 address;
	guint16 port;

	guint32 token;

	gboolean in;

	union
	{
		struct
		{
			guint sources[s_in_num];
		}
		in;

		struct
		{
			struct
			{
				guint32 position;
				gsize offset;
			}
			ack;

			gboolean wait;

			guint sources[s_out_num];
		}
		out;
	}
	d;
};

static gboolean maki_dcc_send_in_read (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gchar buffer[1024];
	gsize bytes_read;
	GIOStatus status;
	makiDCCSend* dcc = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto finish;
	}

	while ((status = g_io_channel_read_chars(source, buffer, 1024, &bytes_read, NULL)) == G_IO_STATUS_NORMAL)
	{
		guint32 pos;

		dcc->position += bytes_read;
		pos = htonl(dcc->position);

		g_io_channel_write_chars(dcc->channel, buffer, bytes_read, NULL, NULL);
		g_io_channel_flush(dcc->channel, NULL);

		g_io_channel_write_chars(source, (gchar*)&pos, sizeof(pos), NULL, NULL);
		g_io_channel_flush(source, NULL);

		if (dcc->size > 0 && dcc->position >= dcc->size)
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
	maki_dcc_send_free(dcc);

	g_io_channel_shutdown(source, FALSE, NULL);
	g_io_channel_unref(source);

	return FALSE;
}

static gboolean maki_dcc_send_in_write (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gint val;
	gchar* dirname;
	socklen_t len = sizeof(val);
	makiDCCSend* dcc = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto error;
	}

	if (getsockopt(g_io_channel_unix_get_fd(source), SOL_SOCKET, SO_ERROR, &val, &len) == -1
	    || val != 0)
	{
		goto error;
	}

	if (g_file_test(dcc->path, G_FILE_TEST_EXISTS))
	{
		goto error;
	}

	dirname = g_path_get_dirname(dcc->path);
	g_mkdir_with_parents(dirname, 0777);
	g_free(dirname);

	if ((dcc->channel = g_io_channel_new_file(dcc->path, "w", NULL)) == NULL)
	{
		goto error;
	}

	g_io_channel_set_close_on_unref(dcc->channel, TRUE);
	g_io_channel_set_encoding(dcc->channel, NULL, NULL);

	dcc->d.in.sources[s_in_write] = 0;
	dcc->d.in.sources[s_in_read] = g_io_add_watch(source, G_IO_IN | G_IO_HUP | G_IO_ERR, maki_dcc_send_in_read, dcc);

	return FALSE;

error:
	maki_dcc_send_free(dcc);

	g_io_channel_shutdown(source, FALSE, NULL);
	g_io_channel_unref(source);

	return FALSE;
}

static gboolean maki_dcc_send_out_write (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gchar buffer[1024];
	gsize bytes_read;
	GIOStatus status;
	makiDCCSend* dcc = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto finish;
	}

	if ((status = g_io_channel_read_chars(dcc->channel, buffer, 1024, &bytes_read, NULL)) != G_IO_STATUS_ERROR)
	{
		if (bytes_read > 0)
		{
			dcc->position += bytes_read;

			i_io_channel_write_chars(source, buffer, bytes_read, NULL, NULL);
			g_io_channel_flush(source, NULL);
		}

		if (dcc->position >= dcc->size || status == G_IO_STATUS_EOF)
		{
			dcc->d.out.wait = TRUE;
			goto finish;
		}
	}

	if (status == G_IO_STATUS_ERROR)
	{
		goto finish;
	}

	return TRUE;

finish:
	if (!dcc->d.out.wait)
	{
		maki_dcc_send_free(dcc);

		g_io_channel_shutdown(source, FALSE, NULL);
		g_io_channel_unref(source);
	}
	else
	{
		dcc->d.out.sources[s_out_write] = 0;
	}

	return FALSE;
}

static gboolean maki_dcc_send_out_read (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gsize bytes_read;
	GIOStatus status;
	makiDCCSend* dcc = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto finish;
	}

	while ((status = g_io_channel_read_chars(source, ((gchar*)&dcc->d.out.ack.position) + dcc->d.out.ack.offset, sizeof(dcc->d.out.ack.position) - dcc->d.out.ack.offset, &bytes_read, NULL)) == G_IO_STATUS_NORMAL)
	{
		dcc->d.out.ack.offset += bytes_read;

		if (dcc->d.out.ack.offset != sizeof(dcc->d.out.ack.position))
		{
			break;
		}

		dcc->d.out.ack.position = ntohl(dcc->d.out.ack.position);
		dcc->d.out.ack.offset = 0;

		if (dcc->d.out.ack.position >= dcc->size)
		{
			goto finish;
		}
	}

	if (status == G_IO_STATUS_ERROR)
	{
		goto finish;
	}

	return TRUE;

finish:
	if (dcc->d.out.wait)
	{
		maki_dcc_send_free(dcc);

		g_io_channel_shutdown(source, FALSE, NULL);
		g_io_channel_unref(source);
	}
	else
	{
		dcc->d.out.sources[s_out_read] = 0;
	}

	return FALSE;
}

static gboolean maki_dcc_send_out_listen (GIOChannel* source, GIOCondition condition, gpointer data)
{
	gint fd;
	GIOChannel* channel;
	makiDCCSend* dcc = data;

	if (condition & (G_IO_HUP | G_IO_ERR))
	{
		goto error;
	}

	fd = g_io_channel_unix_get_fd(source);
	fd = accept(fd, NULL, NULL);

	fcntl(fd, F_SETFL, O_NONBLOCK);

	channel = g_io_channel_unix_new(fd);

	g_io_channel_set_flags(channel, g_io_channel_get_flags(channel) | G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);

	dcc->d.out.sources[s_out_listen] = 0;
	dcc->d.out.sources[s_out_read] = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, maki_dcc_send_out_read, dcc);
	dcc->d.out.sources[s_out_write] = g_io_add_watch(channel, G_IO_OUT | G_IO_HUP | G_IO_ERR, maki_dcc_send_out_write, dcc);

	g_io_channel_shutdown(source, FALSE, NULL);
	g_io_channel_unref(source);

	return FALSE;

error:
	maki_dcc_send_free(dcc);

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

		if (len > 2)
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

makiDCCSend* maki_dcc_send_new_in (makiServer* serv, makiUser* user, const gchar* file_name, guint32 address, guint16 port, goffset file_size, guint32 token)
{
	gchar* downloads_dir;
	makiDCCSend* dcc;

	downloads_dir = maki_instance_config_get_string(serv->instance, "directories", "downloads");

	dcc = g_new(makiDCCSend, 1);

	dcc->channel = NULL;
	dcc->path = g_build_filename(downloads_dir, maki_user_nick(user), file_name, NULL);
	dcc->position = 0;
	dcc->size = file_size;

	dcc->d.in.sources[s_in_read] = 0;
	dcc->d.in.sources[s_in_write] = 0;

	dcc->address = address;
	dcc->port = port;

	dcc->token = token;

	dcc->in = TRUE;

	g_free(downloads_dir);

	return dcc;
}

makiDCCSend* maki_dcc_send_new_out (makiServer* serv, makiUser* user, const gchar* path)
{
	GIOChannel* channel;
	gchar* basename;
	struct stat stbuf;
	makiDCCSend* dcc;

	dcc = g_new(makiDCCSend, 1);

	dcc->channel = NULL;
	dcc->path = g_strdup(path);
	dcc->position = 0;
	dcc->size = 0;

	dcc->address = 0;
	dcc->port = 0;

	dcc->token = 0;

	dcc->in = FALSE;

	dcc->d.out.ack.position = 0;
	dcc->d.out.ack.offset = 0;

	dcc->d.out.wait = FALSE;

	dcc->d.out.sources[s_out_listen] = 0;
	dcc->d.out.sources[s_out_read] = 0;
	dcc->d.out.sources[s_out_write] = 0;

	if (stat(dcc->path, &stbuf) != 0)
	{
		goto error;
	}

	dcc->size = stbuf.st_size;

	/* FIXME */
	for (dcc->port = 2000; dcc->port <= 2010; dcc->port++)
	{
		if ((channel = i_io_channel_unix_new_listen(NULL, dcc->port, TRUE)) != NULL)
		{
			break;
		}
	}

	if (channel == NULL)
	{
		goto error;
	}

	if (serv->stun.addrlen > 0)
	{
		dcc->address = ntohl(((struct sockaddr_in*)&serv->stun.addr)->sin_addr.s_addr);
	}
	else
	{
		struct sockaddr addr;
		socklen_t addrlen = sizeof(addr);

		getsockname(g_io_channel_unix_get_fd(channel), &addr, &addrlen);
		dcc->address = ntohl(((struct sockaddr_in*)&addr)->sin_addr.s_addr);
	}

	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);

	if ((dcc->channel = g_io_channel_new_file(dcc->path, "r", NULL)) == NULL)
	{
		goto error;
	}

	g_io_channel_set_close_on_unref(dcc->channel, TRUE);
	g_io_channel_set_encoding(dcc->channel, NULL, NULL);
	g_io_channel_set_buffered(dcc->channel, FALSE);

	basename = g_path_get_basename(dcc->path);

	if (strstr(basename, " ") == NULL)
	{
		maki_server_send_printf(serv, "PRIVMSG %s :\001DCC SEND %s %" G_GUINT32_FORMAT " %" G_GUINT16_FORMAT " %" G_GUINT64_FORMAT "\001", maki_user_nick(user), basename, dcc->address, dcc->port, dcc->size);
	}
	else
	{
		maki_server_send_printf(serv, "PRIVMSG %s :\001DCC SEND \"%s\" %" G_GUINT32_FORMAT " %" G_GUINT16_FORMAT " %" G_GUINT64_FORMAT "\001", maki_user_nick(user), basename, dcc->address, dcc->port, dcc->size);
	}

	g_free(basename);

	dcc->d.out.sources[s_out_listen] = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, maki_dcc_send_out_listen, dcc);

	return dcc;

error:
	maki_dcc_send_free(dcc);

	return NULL;
}

void maki_dcc_send_free (makiDCCSend* dcc)
{
	if (dcc->in)
	{
		guint i;

		for (i = 0; i < s_in_num; i++)
		{
			if (dcc->d.in.sources[i] != 0)
			{
				g_source_remove(dcc->d.in.sources[i]);
			}
		}
	}
	else
	{
		guint i;

		for (i = 0; i < s_out_num; i++)
		{
			if (dcc->d.out.sources[i] != 0)
			{
				g_source_remove(dcc->d.out.sources[i]);
			}
		}
	}

	if (dcc->channel != NULL)
	{
		g_io_channel_shutdown(dcc->channel, FALSE, NULL);
		g_io_channel_unref(dcc->channel);
	}

	g_free(dcc->path);
	g_free(dcc);
}

gboolean maki_dcc_send_accept (makiDCCSend* dcc)
{
	GIOChannel* channel;

	g_return_val_if_fail(dcc != NULL, FALSE);

	if (dcc->in)
	{
		gchar* address;

		/* FIXME ugly */
		address = g_strdup_printf("%u.%u.%u.%u", (dcc->address & 0xff000000) >> 24, (dcc->address & 0x00ff0000) >> 16, (dcc->address & 0x0000ff00) >> 8, (dcc->address & 0x000000ff) >> 0);

		if ((channel = i_io_channel_unix_new_address(address, dcc->port, TRUE)) == NULL)
		{
			g_free(address);
			return FALSE;
		}

		g_free(address);

		g_io_channel_set_close_on_unref(channel, TRUE);
		g_io_channel_set_encoding(channel, NULL, NULL);

		dcc->d.in.sources[s_in_write] = g_io_add_watch(channel, G_IO_OUT | G_IO_HUP | G_IO_ERR, maki_dcc_send_in_write, dcc);
	}

	return TRUE;
}
