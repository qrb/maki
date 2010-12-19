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

#ifndef H_INSTANCE
#define H_INSTANCE

struct maki_instance;

typedef struct maki_instance makiInstance;

#include "dcc_send.h"
#include "network.h"
#include "server.h"

makiInstance* maki_instance_get_default (void);
makiInstance* maki_instance_new (void);
gboolean maki_instance_config_get_boolean (makiInstance*, const gchar*, const gchar*);
void maki_instance_config_set_boolean (makiInstance*, const gchar*, const gchar*, gboolean);
gint maki_instance_config_get_integer (makiInstance*, const gchar*, const gchar*);
void maki_instance_config_set_integer (makiInstance*, const gchar*, const gchar*, gint);
gchar* maki_instance_config_get_string (makiInstance*, const gchar*, const gchar*);
void maki_instance_config_set_string (makiInstance*, const gchar*, const gchar*, const gchar*);
gchar** maki_instance_config_get_keys (makiInstance*, const gchar*);
gboolean maki_instance_config_exists (makiInstance*, const gchar*, const gchar*);
GMainContext* maki_instance_main_context (makiInstance*);
const gchar* maki_instance_directory (makiInstance*, const gchar*);
makiServer* maki_instance_add_server (makiInstance*, const gchar*, makiServer*);
makiServer* maki_instance_get_server (makiInstance*, const gchar*);
gboolean maki_instance_remove_server (makiInstance*, const gchar*);
guint maki_instance_servers_count (makiInstance*);
void maki_instance_servers_iter (makiInstance*, GHashTableIter*);
guint64 maki_instance_get_dcc_send_id (makiInstance*);
void maki_instance_add_dcc_send (makiInstance*, makiDCCSend*);
gboolean maki_instance_accept_dcc_send (makiInstance*, guint64);
gboolean maki_instance_resume_accept_dcc_send (makiInstance*, gchar*, guint16, goffset, guint32, gboolean);
gboolean maki_instance_resume_dcc_send (makiInstance*, guint64);
gboolean maki_instance_remove_dcc_send (makiInstance*, guint64);
gboolean maki_instance_rename_server (makiInstance*, const gchar*, const gchar*);
guint maki_instance_dcc_sends_count (makiInstance*);
gchar* maki_instance_dcc_send_get (makiInstance*, guint64, const gchar*);
gboolean maki_instance_dcc_send_set (makiInstance*, guint64, const gchar*, const gchar*);
void maki_instance_dcc_sends_xxx (makiInstance*, GArray**, gchar***, gchar***, gchar***, GArray**, GArray**, GArray**, GArray**);
void maki_instance_load_plugins (makiInstance*);
gboolean maki_instance_plugin_method (makiInstance*, const gchar*, const gchar*, gpointer);
makiNetwork* maki_instance_network (makiInstance*);
void maki_instance_free (makiInstance*);

#endif
