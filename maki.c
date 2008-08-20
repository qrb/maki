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

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "maki.h"

struct maki* maki (void)
{
	static struct maki* m = NULL;

	if (G_UNLIKELY(m == NULL))
	{
		m = maki_new();
	}

	return m;
}

struct maki* maki_new (void)
{
	const gchar* home_dir;
	gchar* config_path;
	struct maki* m;

	if ((m = g_new(struct maki, 1)) == NULL)
	{
		return NULL;
	}

	if ((home_dir = g_getenv("HOME")) == NULL)
	{
		home_dir = g_get_home_dir();
	}

	m->bus = g_object_new(MAKI_DBUS_TYPE, NULL);

	m->connections = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, maki_connection_free);

	m->directories.config = g_build_filename(g_get_user_config_dir(), "sushi", NULL);
	m->directories.logs = g_build_filename(g_get_user_data_dir(), "sushi", "logs", NULL);
	m->directories.servers = g_build_filename(g_get_user_config_dir(), "sushi", "servers", NULL);

	config_path = g_build_filename(m->directories.config, "maki", NULL);
	m->config = maki_config_new(config_path);
	g_free(config_path);

	m->message_queue = g_async_queue_new_full(sashimi_message_free);

	m->opt.debug = FALSE;

	m->threads.terminate = FALSE;
	m->threads.messages = g_thread_create(maki_in_runner, m, TRUE, NULL);

	m->loop = g_main_loop_new(NULL, FALSE);

	return m;
}

void maki_free (struct maki* m)
{
	m->threads.terminate = TRUE;
	g_thread_join(m->threads.messages);
	g_async_queue_unref(m->message_queue);

	g_hash_table_destroy(m->connections);

	g_free(m->directories.config);
	g_free(m->directories.logs);
	g_free(m->directories.servers);

	dbus_g_connection_unref(m->bus->bus);
	g_object_unref(m->bus);

	maki_config_free(m->config);

	g_main_loop_quit(m->loop);
	g_main_loop_unref(m->loop);

	g_free(m);
}

static int maki_daemonize (void)
{
	int fd;
	pid_t pid;

	pid = fork();

	if (pid > 0)
	{
		exit(0);
	}
	else if (pid == -1)
	{
		return 1;
	}

	if (setsid() == -1)
	{
		return 1;
	}

	fd = open("/dev/null", O_RDWR);

	if (fd == -1)
	{
		return 1;
	}

	if (dup2(fd, 0) == -1 || dup2(fd, 1) == -1 || dup2(fd, 2) == -1)
	{
		return 1;
	}

	if (fd > 2)
	{
		close(fd);
	}

	return 0;
}

static void maki_signal (int signo)
{
	GTimeVal time;
	GList* list;
	GList* tmp;
	struct maki* m = maki();

	list = g_hash_table_get_values(m->connections);

	for (tmp = list; tmp != NULL; tmp = g_list_next(tmp))
	{
		struct maki_connection* m_conn = tmp->data;

		maki_connection_disconnect(m_conn, SUSHI_QUIT_MESSAGE);
	}

	g_list_free(list);

	g_get_current_time(&time);
	maki_dbus_emit_shutdown(time.tv_sec);

	maki_free(m);

	signal(signo, SIG_DFL);
	raise(signo);
}

int main (int argc, char* argv[])
{
	struct maki* m;

	gboolean opt_daemon = FALSE;
	gboolean opt_debug = TRUE;
	GOptionContext* context;
	GOptionEntry entries[] =
	{
		{ "daemon", 'd', 0, G_OPTION_ARG_NONE, &opt_daemon, N_("Run as daemon"), NULL },
		{ "debug", 0, 0, G_OPTION_ARG_NONE, &opt_debug, N_("Output debug messages"), NULL },
		{ NULL }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(MAKI_NAME, LOCALEDIR);
	textdomain(MAKI_NAME);

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_parse(context, &argc, &argv, NULL);
	g_option_context_free(context);

	if (opt_daemon
	    && maki_daemonize() != 0)
	{
		return 1;
	}

	if (!g_thread_supported())
	{
		g_thread_init(NULL);
	}

	dbus_g_thread_init();
	g_type_init();

	if ((m = maki()) == NULL)
	{
		return 1;
	}

	m->opt.debug = opt_debug;

	signal(SIGINT, maki_signal);
	signal(SIGHUP, maki_signal);
	signal(SIGTERM, maki_signal);
	signal(SIGQUIT, maki_signal);

	if (g_mkdir_with_parents(m->directories.config, S_IRUSR | S_IWUSR | S_IXUSR) != 0
	    || g_mkdir_with_parents(m->directories.logs, S_IRUSR | S_IWUSR | S_IXUSR) != 0
	    || g_mkdir_with_parents(m->directories.servers, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
	{
		return 1;
	}

	maki_servers();

	g_main_loop_run(m->loop);

	return 0;
}
