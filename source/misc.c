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

#include "instance.h"
#include "log.h"
#include "misc.h"

#include "ilib.h"

#include <gio/gio.h>

gboolean maki_config_is_empty (const gchar* value)
{
	gboolean ret = TRUE;

	if (value != NULL && value[0] != '\0')
	{
		ret = FALSE;
	}

	return ret;
}

gboolean maki_config_is_empty_list (gchar** list)
{
	gboolean ret = TRUE;

	if (list != NULL && g_strv_length(list) > 0)
	{
		ret = FALSE;
	}

	return ret;
}

void maki_debug (const gchar* format, ...)
{
	/* FIXME leak */
	static GDataOutputStream* stream = NULL;
	gchar* message;
	va_list args;

	if (!opt_verbose)
	{
		return;
	}

	if (G_UNLIKELY(stream == NULL))
	{
		GFile* dir;
		GFile* file;
		GFileOutputStream* file_output;
		gchar* path;

		path = g_build_filename(g_get_user_cache_dir(), "sushi", "maki.txt", NULL);

		file = g_file_new_for_path(path);

		dir = g_file_get_parent(file);
		g_file_make_directory_with_parents(dir, NULL, NULL);
		g_object_unref(dir);

		g_file_delete(file, NULL, NULL);

		if ((file_output = g_file_create(file, G_FILE_CREATE_PRIVATE, NULL, NULL)) != NULL)
		{
			stream = g_data_output_stream_new(G_OUTPUT_STREAM(file_output));
			g_object_unref(file_output);
		}
		else
		{
			g_printerr("%s\n", _("Could not open debug log file."));
		}

		g_object_unref(file);

		g_free(path);
	}

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);

	g_printerr("%s", message);

	if (G_LIKELY(stream != NULL))
	{
		g_data_output_stream_put_string(stream, message, NULL, NULL);
		g_output_stream_flush(G_OUTPUT_STREAM(stream), NULL, NULL);
	}

	g_free(message);
}

void maki_log (makiServer* serv, const gchar* name, const gchar* format, ...)
{
	gchar* file;
	gchar* file_tmp;
	gchar* file_format;
	gchar* tmp;
	makiLog* log;
	va_list args;
	makiInstance* inst = maki_instance_get_default();

	if (!maki_instance_config_get_boolean(inst, "logging", "enabled"))
	{
		return;
	}

	file_format = maki_instance_config_get_string(inst, "logging", "format");
	file_tmp = i_strreplace(file_format, "$n", name, 0);
	file = i_get_current_time_string(file_tmp);

	g_free(file_format);
	g_free(file_tmp);

	if (file == NULL)
	{
		return;
	}

	if ((log = g_hash_table_lookup(serv->logs, file)) == NULL)
	{
		if ((log = maki_log_new(inst, maki_server_name(serv), file)) == NULL)
		{
			return;
		}

		g_hash_table_insert(serv->logs, g_strdup(file), log);
	}

	g_free(file);

	va_start(args, format);
	tmp = g_strdup_vprintf(format, args);
	va_end(args);

	maki_log_write(log, tmp);

	g_free(tmp);
}

void maki_ensure_string (gchar** string)
{
	if (*string == NULL)
	{
		*string = g_strdup("");
	}
}

void maki_ensure_string_array (gchar*** string_array)
{
	if (*string_array == NULL)
	{
		*string_array = g_new(gchar*, 1);
		(*string_array)[0] = NULL;
	}
}

GVariantBuilder*
maki_variant_builder_array_string (gchar** str)
{
	GVariantBuilder* builder;
	guint i;

	builder = g_variant_builder_new(G_VARIANT_TYPE("as"));

	for (i = 0; str[i] != NULL; i++)
	{
		g_variant_builder_add(builder, "s", str[i]);
	}

	return builder;
}

GVariantBuilder*
maki_variant_builder_array_uint64 (GArray* array)
{
	GVariantBuilder* builder;
	guint i;

	builder = g_variant_builder_new(G_VARIANT_TYPE("at"));

	for (i = 0; i < array->len; i++)
	{
		g_variant_builder_add(builder, "t", g_array_index(array, guint64, i));
	}

	return builder;
}

gchar**
maki_variant_iter_array_string (GVariantIter* iter)
{
	gchar** str;
	gchar* t;
	guint i;

	str = g_new(gchar*, g_variant_iter_n_children(iter) + 1);
	i = 0;

	while (g_variant_iter_next(iter, "s", &t))
	{
		str[i] = t;
		i++;
	}

	str[i] = NULL;

	return str;
}
