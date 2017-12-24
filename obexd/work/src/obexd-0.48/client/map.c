/*
 *
 *  OBEX Client
 *
 *  Copyright (C) 2011  Bartosz Szatkowski <bulislaw@linux.com> for Comarch
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <gdbus.h>

#include <gobex-apparam.h>

#include "dbus.h"
#include "log.h"
#include "map_ap.h"

#include "map.h"
#include "transfer.h"
#include "session.h"
#include "driver.h"

#define OBEX_MAS_UUID \
	"\xBB\x58\x2B\x40\x42\x0C\x11\xDB\xB0\xDE\x08\x00\x20\x0C\x9A\x66"
#define OBEX_MAS_UUID_LEN 16

#define MAP_INTERFACE "org.bluez.obex.MessageAccess"
#define MAP_MSG_INTERFACE "org.bluez.obex.Message"
#define ERROR_INTERFACE "org.bluez.obex.Error"
#define MAS_UUID "00001132-0000-1000-8000-00805f9b34fb"

#define DEFAULT_COUNT 1024
#define DEFAULT_OFFSET 0

#define CHARSET_UTF8 1

static const char * const filter_list[] = {
	"subject",
	"timestamp",
	"sender",
	"sender-address",
	"recipient",
	"recipient-address",
	"type",
	"size",
	"status",
	"text",
	"attachment",
	"priority",
	"read",
	"sent",
	"protected",
	"replyto",
	NULL
};

#define FILTER_BIT_MAX	15
#define FILTER_ALL	0xFF

#define STATUS_READ 0
#define STATUS_DELETE 1
#define FILLER_BYTE 0x30

struct map_data {
	struct obc_session *session;
	DBusMessage *msg;
	GHashTable *messages;
};

#define MAP_MSG_FLAG_PRIORITY	0x01
#define MAP_MSG_FLAG_READ	0x02
#define MAP_MSG_FLAG_SENT	0x04
#define MAP_MSG_FLAG_PROTECTED	0x08

struct map_msg {
	struct map_data *data;
	char *path;
	char *handle;
	char *subject;
	char *timestamp;
	char *sender;
	char *sender_address;
	char *replyto;
	char *recipient;
	char *recipient_address;
	char *type;
	uint64_t size;
	char *status;
	uint8_t flags;
	DBusMessage *msg;
};

struct map_parser {
	struct map_data *data;
	DBusMessageIter *iter;
};

static DBusConnection *conn = NULL;

static void simple_cb(struct obc_session *session,
						struct obc_transfer *transfer,
						GError *err, void *user_data)
{
	DBusMessage *reply;
	struct map_data *map = user_data;

	if (err != NULL)
		reply = g_dbus_create_error(map->msg,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
	else
		reply = dbus_message_new_method_return(map->msg);

	g_dbus_send_message(conn, reply);
	dbus_message_unref(map->msg);
}

static DBusMessage *map_setpath(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	struct map_data *map = user_data;
	const char *folder;
	GError *err = NULL;

	if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &folder,
						DBUS_TYPE_INVALID) == FALSE)
		return g_dbus_create_error(message,
					ERROR_INTERFACE ".InvalidArguments",
					NULL);

	obc_session_setpath(map->session, folder, simple_cb, map, &err);
	if (err != NULL) {
		DBusMessage *reply;
		reply =  g_dbus_create_error(message,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
		g_error_free(err);
		return reply;
	}

	map->msg = dbus_message_ref(message);

	return NULL;
}

static void folder_element(GMarkupParseContext *ctxt, const gchar *element,
				const gchar **names, const gchar **values,
				gpointer user_data, GError **gerr)
{
	DBusMessageIter dict, *iter = user_data;
	const gchar *key;
	gint i;

	if (strcasecmp("folder", element) != 0)
		return;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	for (i = 0, key = names[i]; key; key = names[++i]) {
		if (strcasecmp("name", key) == 0)
			obex_dbus_dict_append(&dict, "Name", DBUS_TYPE_STRING,
								&values[i]);
	}

	dbus_message_iter_close_container(iter, &dict);
}

static const GMarkupParser folder_parser = {
	folder_element,
	NULL,
	NULL,
	NULL,
	NULL
};

static void folder_listing_cb(struct obc_session *session,
						struct obc_transfer *transfer,
						GError *err, void *user_data)
{
	struct map_data *map = user_data;
	GMarkupParseContext *ctxt;
	DBusMessage *reply;
	DBusMessageIter iter, array;
	char *contents;
	size_t size;
	int perr;

	if (err != NULL) {
		reply = g_dbus_create_error(map->msg,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
		goto done;
	}

	perr = obc_transfer_get_contents(transfer, &contents, &size);
	if (perr < 0) {
		reply = g_dbus_create_error(map->msg,
						ERROR_INTERFACE ".Failed",
						"Error reading contents: %s",
						strerror(-perr));
		goto done;
	}

	reply = dbus_message_new_method_return(map->msg);
	if (reply == NULL)
		return;

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
			DBUS_TYPE_ARRAY_AS_STRING
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &array);
	ctxt = g_markup_parse_context_new(&folder_parser, 0, &array, NULL);
	g_markup_parse_context_parse(ctxt, contents, size, NULL);
	g_markup_parse_context_free(ctxt);
	dbus_message_iter_close_container(&iter, &array);
	g_free(contents);

done:
	g_dbus_send_message(conn, reply);
	dbus_message_unref(map->msg);
}

static DBusMessage *get_folder_listing(struct map_data *map,
							DBusMessage *message,
							GObexApparam *apparam)
{
	struct obc_transfer *transfer;
	GError *err = NULL;
	DBusMessage *reply;

	transfer = obc_transfer_get("x-obex/folder-listing", NULL, NULL, &err);
	if (transfer == NULL) {
		g_obex_apparam_free(apparam);
		goto fail;
	}

	obc_transfer_set_apparam(transfer, apparam);

	if (obc_session_queue(map->session, transfer, folder_listing_cb, map,
								&err)) {
		map->msg = dbus_message_ref(message);
		return NULL;
	}

fail:
	reply = g_dbus_create_error(message, ERROR_INTERFACE ".Failed", "%s",
								err->message);
	g_error_free(err);
	return reply;
}

static GObexApparam *parse_offset(GObexApparam *apparam, DBusMessageIter *iter)
{
	guint16 num;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_UINT16)
		return NULL;

	dbus_message_iter_get_basic(iter, &num);

	return g_obex_apparam_set_uint16(apparam, MAP_AP_STARTOFFSET, num);
}

static GObexApparam *parse_max_count(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	guint16 num;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_UINT16)
		return NULL;

	dbus_message_iter_get_basic(iter, &num);

	return g_obex_apparam_set_uint16(apparam, MAP_AP_MAXLISTCOUNT, num);
}

static GObexApparam *parse_folder_filters(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	DBusMessageIter array;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return NULL;

	dbus_message_iter_recurse(iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
		const char *key;
		DBusMessageIter value, entry;

		dbus_message_iter_recurse(&array, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (strcasecmp(key, "Offset") == 0) {
			if (parse_offset(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "MaxCount") == 0) {
			if (parse_max_count(apparam, &value) == NULL)
				return NULL;
		}

		dbus_message_iter_next(&array);
	}

	return apparam;
}

static DBusMessage *map_list_folders(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	struct map_data *map = user_data;
	GObexApparam *apparam;
	DBusMessageIter args;

	dbus_message_iter_init(message, &args);

	apparam = g_obex_apparam_set_uint16(NULL, MAP_AP_MAXLISTCOUNT,
							DEFAULT_COUNT);
	apparam = g_obex_apparam_set_uint16(apparam, MAP_AP_STARTOFFSET,
							DEFAULT_OFFSET);

	if (parse_folder_filters(apparam, &args) == NULL) {
		g_obex_apparam_free(apparam);
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);
	}

	return get_folder_listing(map, message, apparam);
}

static void map_msg_free(void *data)
{
	struct map_msg *msg = data;

	g_free(msg->path);
	g_free(msg->subject);
	g_free(msg->handle);
	g_free(msg->timestamp);
	g_free(msg->sender);
	g_free(msg->sender_address);
	g_free(msg->replyto);
	g_free(msg->recipient);
	g_free(msg->recipient_address);
	g_free(msg->type);
	g_free(msg->status);
	g_free(msg);
}

static DBusMessage *map_msg_get(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	struct map_msg *msg = user_data;
	struct obc_transfer *transfer;
	const char *target_file;
	gboolean attachment;
	GError *err = NULL;
	DBusMessage *reply;
	GObexApparam *apparam;

	if (dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &target_file,
				DBUS_TYPE_BOOLEAN, &attachment,
				DBUS_TYPE_INVALID) == FALSE)
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);

	transfer = obc_transfer_get("x-bt/message", msg->handle, target_file,
									&err);
	if (transfer == NULL)
		goto fail;

	apparam = g_obex_apparam_set_uint8(NULL, MAP_AP_ATTACHMENT,
								attachment);
	apparam = g_obex_apparam_set_uint8(apparam, MAP_AP_CHARSET,
								CHARSET_UTF8);

	obc_transfer_set_apparam(transfer, apparam);

	if (!obc_session_queue(msg->data->session, transfer, NULL, NULL, &err))
		goto fail;

	return obc_transfer_create_dbus_reply(transfer, message);

fail:
	reply = g_dbus_create_error(message, ERROR_INTERFACE ".Failed", "%s",
								err->message);
	g_error_free(err);
	return reply;
}

static void set_message_status_cb(struct obc_session *session,
						struct obc_transfer *transfer,
						GError *err, void *user_data)
{
	struct map_msg *msg = user_data;
	DBusMessage *reply;

	if (err != NULL) {
		reply = g_dbus_create_error(msg->msg,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
		goto done;
	}

	reply = dbus_message_new_method_return(msg->msg);
	if (reply == NULL) {
		reply = g_dbus_create_error(msg->msg,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
	}

done:
	g_dbus_send_message(conn, reply);
	dbus_message_unref(msg->msg);
	msg->msg = NULL;
}

static DBusMessage *map_msg_set_property(DBusConnection *connection,
						DBusMessage *message,
						void *user_data)
{
	struct map_msg *msg = user_data;
	struct obc_transfer *transfer;
	char *property;
	gboolean status;
	GError *err = NULL;
	DBusMessage *reply;
	GObexApparam *apparam;
	char contents[2];
	int op;
	DBusMessageIter args, variant;

	dbus_message_iter_init(message, &args);
	if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);

	dbus_message_iter_get_basic(&args, &property);
	dbus_message_iter_next(&args);
	if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT)
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);

	dbus_message_iter_recurse(&args, &variant);
	if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_BOOLEAN)
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);

	dbus_message_iter_get_basic(&variant, &status);

	/* MAP supports modifying only these two properties. */
	if (property && strcasecmp(property, "Read") == 0) {
		op = STATUS_READ;
		if (status)
			msg->flags |= MAP_MSG_FLAG_READ;
		else
			msg->flags &= ~MAP_MSG_FLAG_READ;
	} else if (property && strcasecmp(property, "Deleted") == 0)
		op = STATUS_DELETE;
	else {
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);
	}

	contents[0] = FILLER_BYTE;
	contents[1] = '\0';

	transfer = obc_transfer_put("x-bt/messageStatus", msg->handle, NULL,
							contents,
							sizeof(contents), &err);
	if (transfer == NULL)
		goto fail;

	apparam = g_obex_apparam_set_uint8(NULL, MAP_AP_STATUSINDICATOR,
								op);
	apparam = g_obex_apparam_set_uint8(apparam, MAP_AP_STATUSVALUE,
								status);
	obc_transfer_set_apparam(transfer, apparam);

	if (!obc_session_queue(msg->data->session, transfer,
				set_message_status_cb, msg, &err))
		goto fail;

	msg->msg = dbus_message_ref(message);
	return NULL;

fail:
	reply = g_dbus_create_error(message, ERROR_INTERFACE ".Failed", "%s",
								err->message);
	g_error_free(err);
	return reply;
}

static DBusMessage *map_msg_get_properties(DBusConnection *connection,
						DBusMessage *message,
						void *user_data)
{
	struct map_msg *msg = user_data;
	GError *err = NULL;
	DBusMessage *reply;
	DBusMessageIter iter, data_array;
	gboolean flag;

	reply = dbus_message_new_method_return(message);
	if (reply == NULL) {
		reply = g_dbus_create_error(message,
						ERROR_INTERFACE ".Failed",
						NULL);
		goto done;
	}

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					&data_array);


	obex_dbus_dict_append(&data_array, "Subject",
				DBUS_TYPE_STRING, &msg->subject);
	obex_dbus_dict_append(&data_array, "Timestamp",
				DBUS_TYPE_STRING, &msg->timestamp);
	obex_dbus_dict_append(&data_array, "Sender",
				DBUS_TYPE_STRING, &msg->sender);
	obex_dbus_dict_append(&data_array, "SenderAddress",
				DBUS_TYPE_STRING, &msg->sender_address);
	obex_dbus_dict_append(&data_array, "ReplyTo",
				DBUS_TYPE_STRING, &msg->replyto);
	obex_dbus_dict_append(&data_array, "Recipient",
				DBUS_TYPE_STRING, &msg->recipient);
	obex_dbus_dict_append(&data_array, "RecipientAddress",
				DBUS_TYPE_STRING, &msg->recipient_address);
	obex_dbus_dict_append(&data_array, "Type",
				DBUS_TYPE_STRING, &msg->type);
	obex_dbus_dict_append(&data_array, "Status",
				DBUS_TYPE_STRING, &msg->status);
	obex_dbus_dict_append(&data_array, "Size",
				DBUS_TYPE_UINT64, &msg->size);

	flag = (msg->flags & MAP_MSG_FLAG_PRIORITY) != 0;
	obex_dbus_dict_append(&data_array, "Priority",
				DBUS_TYPE_BOOLEAN, &flag);

	flag = (msg->flags & MAP_MSG_FLAG_READ) != 0;
	obex_dbus_dict_append(&data_array, "Read",
				DBUS_TYPE_BOOLEAN, &flag);

	flag = (msg->flags & MAP_MSG_FLAG_SENT) != 0;
	obex_dbus_dict_append(&data_array, "Sent",
				DBUS_TYPE_BOOLEAN, &flag);

	flag = (msg->flags & MAP_MSG_FLAG_PROTECTED) != 0;
	obex_dbus_dict_append(&data_array, "Protected",
				DBUS_TYPE_BOOLEAN, &flag);

	dbus_message_iter_close_container(&iter, &data_array);


done:
	if (err)
		g_error_free(err);

	return reply;
}

static const GDBusMethodTable map_msg_methods[] = {
	{ GDBUS_METHOD("Get",
			GDBUS_ARGS({ "targetfile", "s" },
						{ "attachment", "b" }),
			GDBUS_ARGS({ "transfer", "o" },
						{ "properties", "a{sv}" }),
			map_msg_get) },
	{ GDBUS_METHOD("GetProperties",
			NULL,
			GDBUS_ARGS({ "properties", "a{sv}" }),
			map_msg_get_properties) },
	{ GDBUS_ASYNC_METHOD("SetProperty",
			GDBUS_ARGS({ "property", "sv" }), NULL,
			map_msg_set_property) },
	{ }
};

static struct map_msg *map_msg_create(struct map_data *data, const char *handle)
{
	struct map_msg *msg;

	msg = g_new0(struct map_msg, 1);
	msg->data = data;
	msg->path = g_strdup_printf("%s/message%s",
					obc_session_get_path(data->session),
					handle);

	if (!g_dbus_register_interface(conn, msg->path, MAP_MSG_INTERFACE,
						map_msg_methods, NULL, NULL,
						msg, map_msg_free)) {
		map_msg_free(msg);
		return NULL;
	}

	msg->handle = g_strdup(handle);
	g_hash_table_insert(data->messages, msg->handle, msg);

	return msg;
}

static void parse_subject(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->subject);
	msg->subject = g_strdup(value);
	obex_dbus_dict_append(iter, "Subject", DBUS_TYPE_STRING, &value);
}

static void parse_datetime(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->timestamp);
	msg->timestamp = g_strdup(value);
	obex_dbus_dict_append(iter, "Timestamp", DBUS_TYPE_STRING, &value);
}

static void parse_sender(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->sender);
	msg->sender = g_strdup(value);
	obex_dbus_dict_append(iter, "Sender", DBUS_TYPE_STRING, &value);
}

static void parse_sender_address(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->sender_address);
	msg->sender_address = g_strdup(value);
	obex_dbus_dict_append(iter, "SenderAddress", DBUS_TYPE_STRING,
								&value);
}

static void parse_replyto(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->replyto);
	msg->replyto = g_strdup(value);
	obex_dbus_dict_append(iter, "ReplyTo", DBUS_TYPE_STRING, &value);
}

static void parse_recipient(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->recipient);
	msg->recipient = g_strdup(value);
	obex_dbus_dict_append(iter, "Recipient", DBUS_TYPE_STRING, &value);
}

static void parse_recipient_address(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->recipient_address);
	msg->recipient_address = g_strdup(value);
	obex_dbus_dict_append(iter, "RecipientAddress", DBUS_TYPE_STRING,
								&value);
}

static void parse_type(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->type);
	msg->type = g_strdup(value);
	obex_dbus_dict_append(iter, "Type", DBUS_TYPE_STRING, &value);
}

static void parse_status(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	g_free(msg->status);
	msg->status = g_strdup(value);
	obex_dbus_dict_append(iter, "Status", DBUS_TYPE_STRING, &value);
}

static void parse_size(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	msg->size = g_ascii_strtoll(value, NULL, 10);
	obex_dbus_dict_append(iter, "Size", DBUS_TYPE_UINT64, &msg->size);
}

static void parse_priority(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	gboolean flag = strcasecmp(value, "no") != 0;

	if (flag)
		msg->flags |= MAP_MSG_FLAG_PRIORITY;
	else
		msg->flags &= ~MAP_MSG_FLAG_PRIORITY;

	obex_dbus_dict_append(iter, "Priority", DBUS_TYPE_BOOLEAN, &flag);
}

static void parse_read(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	gboolean flag = strcasecmp(value, "no") != 0;

	if (flag)
		msg->flags |= MAP_MSG_FLAG_READ;
	else
		msg->flags &= ~MAP_MSG_FLAG_READ;

	obex_dbus_dict_append(iter, "Read", DBUS_TYPE_BOOLEAN, &flag);
}

static void parse_sent(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	gboolean flag = strcasecmp(value, "no") != 0;

	if (flag)
		msg->flags |= MAP_MSG_FLAG_SENT;
	else
		msg->flags &= ~MAP_MSG_FLAG_SENT;

	obex_dbus_dict_append(iter, "Sent", DBUS_TYPE_BOOLEAN, &flag);
}

static void parse_protected(struct map_msg *msg, const char *value,
							DBusMessageIter *iter)
{
	gboolean flag = strcasecmp(value, "no") != 0;

	if (flag)
		msg->flags |= MAP_MSG_FLAG_PROTECTED;
	else
		msg->flags &= ~MAP_MSG_FLAG_PROTECTED;

	obex_dbus_dict_append(iter, "Protected", DBUS_TYPE_BOOLEAN, &flag);
}

static struct map_msg_parser {
	const char *name;
	void (*func) (struct map_msg *msg, const char *value,
							DBusMessageIter *iter);
} msg_parsers[] = {
		{ "subject", parse_subject },
		{ "datetime", parse_datetime },
		{ "sender_name", parse_sender },
		{ "sender_addressing", parse_sender_address },
		{ "replyto_addressing", parse_replyto },
		{ "recipient_name", parse_recipient },
		{ "recipient_addressing", parse_recipient_address },
		{ "type", parse_type },
		{ "reception_status", parse_status },
		{ "size", parse_size },
		{ "priority", parse_priority },
		{ "read", parse_read },
		{ "sent", parse_sent },
		{ "protected", parse_protected },
		{ }
};

static void msg_element(GMarkupParseContext *ctxt, const gchar *element,
				const gchar **names, const gchar **values,
				gpointer user_data, GError **gerr)
{
	struct map_parser *parser = user_data;
	struct map_data *data = parser->data;
	DBusMessageIter entry, dict, *iter = parser->iter;
	struct map_msg *msg;
	const gchar *key;
	gint i;

	if (strcasecmp("msg", element) != 0)
		return;

	for (i = 0, key = names[i]; key; key = names[++i]) {
		if (strcasecmp(key, "handle") == 0)
			break;
	}

	msg = g_hash_table_lookup(data->messages, values[i]);
	if (msg == NULL) {
		msg = map_msg_create(data, values[i]);
		if (msg == NULL)
			return;
	}

	dbus_message_iter_open_container(iter, DBUS_TYPE_DICT_ENTRY, NULL,
								&entry);

	dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH,
								&msg->path);

	dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY,
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					&dict);

	for (i = 0, key = names[i]; key; key = names[++i]) {
		struct map_msg_parser *parser;

		for (parser = msg_parsers; parser && parser->name; parser++) {
			if (strcasecmp(key, parser->name) == 0) {
				parser->func(msg, values[i], &dict);
				break;
			}
		}
	}

	dbus_message_iter_close_container(&entry, &dict);
	dbus_message_iter_close_container(iter, &entry);
}

static const GMarkupParser msg_parser = {
	msg_element,
	NULL,
	NULL,
	NULL,
	NULL
};

static void message_listing_cb(struct obc_session *session,
						struct obc_transfer *transfer,
						GError *err, void *user_data)
{
	struct map_data *map = user_data;
	struct map_parser *parser;
	GMarkupParseContext *ctxt;
	DBusMessage *reply;
	DBusMessageIter iter, array;
	char *contents;
	size_t size;
	int perr;

	if (err != NULL) {
		reply = g_dbus_create_error(map->msg,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
		goto done;
	}

	perr = obc_transfer_get_contents(transfer, &contents, &size);
	if (perr < 0) {
		reply = g_dbus_create_error(map->msg,
						ERROR_INTERFACE ".Failed",
						"Error reading contents: %s",
						strerror(-perr));
		goto done;
	}

	reply = dbus_message_new_method_return(map->msg);
	if (reply == NULL)
		return;

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_OBJECT_PATH_AS_STRING
					DBUS_TYPE_ARRAY_AS_STRING
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					&array);

	parser = g_new(struct map_parser, 1);
	parser->data = map;
	parser->iter = &array;

	ctxt = g_markup_parse_context_new(&msg_parser, 0, parser, NULL);
	g_markup_parse_context_parse(ctxt, contents, size, NULL);
	g_markup_parse_context_free(ctxt);
	dbus_message_iter_close_container(&iter, &array);
	g_free(contents);
	g_free(parser);

done:
	g_dbus_send_message(conn, reply);
	dbus_message_unref(map->msg);
}

static DBusMessage *get_message_listing(struct map_data *map,
							DBusMessage *message,
							const char *folder,
							GObexApparam *apparam)
{
	struct obc_transfer *transfer;
	GError *err = NULL;
	DBusMessage *reply;

	transfer = obc_transfer_get("x-bt/MAP-msg-listing", folder, NULL, &err);
	if (transfer == NULL) {
		g_obex_apparam_free(apparam);
		goto fail;
	}

	obc_transfer_set_apparam(transfer, apparam);

	if (obc_session_queue(map->session, transfer, message_listing_cb, map,
								&err)) {
		map->msg = dbus_message_ref(message);
		return NULL;
	}

fail:
	reply = g_dbus_create_error(message, ERROR_INTERFACE ".Failed", "%s",
								err->message);
	g_error_free(err);
	return reply;
}

static uint64_t get_filter_mask(const char *filterstr)
{
	int i;

	if (!filterstr)
		return 0;

	if (!g_ascii_strcasecmp(filterstr, "ALL"))
		return FILTER_ALL;

	for (i = 0; filter_list[i] != NULL; i++)
		if (!g_ascii_strcasecmp(filterstr, filter_list[i]))
			return 1ULL << i;

	return 0;
}

static int set_field(guint32 *filter, const char *filterstr)
{
	guint64 mask;

	mask = get_filter_mask(filterstr);

	if (mask == 0)
		return -EINVAL;

	*filter |= mask;
	return 0;
}

static GObexApparam *parse_fields(GObexApparam *apparam, DBusMessageIter *iter)
{
	DBusMessageIter array;
	guint32 filter = 0;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return NULL;

	dbus_message_iter_recurse(iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
		const char *string;

		dbus_message_iter_get_basic(&array, &string);

		if (set_field(&filter, string) < 0)
			return NULL;

		dbus_message_iter_next(&array);
	}

	return g_obex_apparam_set_uint32(apparam, MAP_AP_PARAMETERMASK,
								filter);
}

static GObexApparam *parse_filter_type(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	DBusMessageIter array;
	guint8 types = 0;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return NULL;

	dbus_message_iter_recurse(iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
		const char *string;

		dbus_message_iter_get_basic(&array, &string);

		if (!g_ascii_strcasecmp(string, "sms"))
			types |= 0x03; /* SMS_GSM and SMS_CDMA */
		else if (!g_ascii_strcasecmp(string, "email"))
			types |= 0x04; /* EMAIL */
		else if (!g_ascii_strcasecmp(string, "mms"))
			types |= 0x08; /* MMS */
		else
			return NULL;
	}

	return g_obex_apparam_set_uint8(apparam, MAP_AP_FILTERMESSAGETYPE,
									types);
}

static GObexApparam *parse_period_begin(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	const char *string;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING)
		return NULL;

	dbus_message_iter_get_basic(iter, &string);

	return g_obex_apparam_set_string(apparam, MAP_AP_FILTERPERIODBEGIN,
								string);
}

static GObexApparam *parse_period_end(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	const char *string;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING)
		return NULL;

	dbus_message_iter_get_basic(iter, &string);

	return g_obex_apparam_set_string(apparam, MAP_AP_FILTERPERIODEND,
								string);
}

static GObexApparam *parse_filter_read(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	guint8 status = 0;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_BOOLEAN)
		return NULL;

	dbus_message_iter_get_basic(iter, &status);

	status = (status) ? 0x01 : 0x02;

	return g_obex_apparam_set_uint8(apparam, MAP_AP_FILTERREADSTATUS,
								status);
}

static GObexApparam *parse_filter_recipient(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	const char *string;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING)
		return NULL;

	dbus_message_iter_get_basic(iter, &string);

	return g_obex_apparam_set_string(apparam, MAP_AP_FILTERRECIPIENT,
								string);
}

static GObexApparam *parse_filter_sender(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	const char *string;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING)
		return NULL;

	dbus_message_iter_get_basic(iter, &string);

	return g_obex_apparam_set_string(apparam, MAP_AP_FILTERORIGINATOR,
								string);
}

static GObexApparam *parse_filter_priority(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	guint8 priority;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_BOOLEAN)
		return NULL;

	dbus_message_iter_get_basic(iter, &priority);

	priority = (priority) ? 0x01 : 0x02;

	return g_obex_apparam_set_uint8(apparam, MAP_AP_FILTERPRIORITY,
								priority);
}

static GObexApparam *parse_message_filters(GObexApparam *apparam,
							DBusMessageIter *iter)
{
	DBusMessageIter array;

	DBG("");

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return NULL;

	dbus_message_iter_recurse(iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
		const char *key;
		DBusMessageIter value, entry;

		dbus_message_iter_recurse(&array, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (strcasecmp(key, "Offset") == 0) {
			if (parse_offset(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "MaxCount") == 0) {
			if (parse_max_count(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "Fields") == 0) {
			if (parse_fields(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "Types") == 0) {
			if (parse_filter_type(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "PeriodBegin") == 0) {
			if (parse_period_begin(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "PeriodEnd") == 0) {
			if (parse_period_end(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "Read") == 0) {
			if (parse_filter_read(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "Recipient") == 0) {
			if (parse_filter_recipient(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "Sender") == 0) {
			if (parse_filter_sender(apparam, &value) == NULL)
				return NULL;
		} else if (strcasecmp(key, "Priority") == 0) {
			if (parse_filter_priority(apparam, &value) == NULL)
				return NULL;
		}

		dbus_message_iter_next(&array);
	}

	return apparam;
}

static DBusMessage *map_list_messages(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	struct map_data *map = user_data;
	const char *folder;
	GObexApparam *apparam;
	DBusMessageIter args;

	dbus_message_iter_init(message, &args);

	if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);

	dbus_message_iter_get_basic(&args, &folder);

	apparam = g_obex_apparam_set_uint16(NULL, MAP_AP_MAXLISTCOUNT,
							DEFAULT_COUNT);
	apparam = g_obex_apparam_set_uint16(apparam, MAP_AP_STARTOFFSET,
							DEFAULT_OFFSET);

	dbus_message_iter_next(&args);

	if (parse_message_filters(apparam, &args) == NULL) {
		g_obex_apparam_free(apparam);
		return g_dbus_create_error(message,
				ERROR_INTERFACE ".InvalidArguments", NULL);
	}

	return get_message_listing(map, message, folder, apparam);
}

static gchar **get_filter_strs(uint64_t filter, gint *size)
{
	gchar **list, **item;
	gint i;

	list = g_malloc0(sizeof(gchar **) * (FILTER_BIT_MAX + 2));

	item = list;

	for (i = 0; filter_list[i] != NULL; i++)
		if (filter & (1ULL << i))
			*(item++) = g_strdup(filter_list[i]);

	*item = NULL;
	*size = item - list;
	return list;
}

static DBusMessage *map_list_filter_fields(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	gchar **filters = NULL;
	gint size;
	DBusMessage *reply;

	filters = get_filter_strs(FILTER_ALL, &size);
	reply = dbus_message_new_method_return(message);
	dbus_message_append_args(reply, DBUS_TYPE_ARRAY,
				DBUS_TYPE_STRING, &filters, size,
				DBUS_TYPE_INVALID);

	g_strfreev(filters);
	return reply;
}

static void update_inbox_cb(struct obc_session *session,
				struct obc_transfer *transfer,
				GError *err, void *user_data)
{
	struct map_data *map = user_data;
	DBusMessage *reply;

	if (err != NULL) {
		reply = g_dbus_create_error(map->msg,
						ERROR_INTERFACE ".Failed",
						"%s", err->message);
		goto done;
	}

	reply = dbus_message_new_method_return(map->msg);

done:
	g_dbus_send_message(conn, reply);
	dbus_message_unref(map->msg);
}

static DBusMessage *map_update_inbox(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	struct map_data *map = user_data;
	DBusMessage *reply;
	char contents[2];
	struct obc_transfer *transfer;
	GError *err = NULL;

	contents[0] = FILLER_BYTE;
	contents[1] = '\0';

	transfer = obc_transfer_put("x-bt/MAP-messageUpdate", NULL, NULL,
						contents, sizeof(contents),
						&err);
	if (transfer == NULL)
		goto fail;

	if (!obc_session_queue(map->session, transfer, update_inbox_cb,
								map, &err))
		goto fail;

	map->msg = dbus_message_ref(message);

	return NULL;

fail:
	reply = g_dbus_create_error(message, ERROR_INTERFACE ".Failed", "%s",
								err->message);
	g_error_free(err);
	return reply;
}

static const GDBusMethodTable map_methods[] = {
	{ GDBUS_ASYNC_METHOD("SetFolder",
				GDBUS_ARGS({ "name", "s" }), NULL,
				map_setpath) },
	{ GDBUS_ASYNC_METHOD("ListFolders",
			GDBUS_ARGS({ "filters", "a{sv}" }),
			GDBUS_ARGS({ "content", "aa{sv}" }),
			map_list_folders) },
	{ GDBUS_ASYNC_METHOD("ListMessages",
			GDBUS_ARGS({ "folder", "s" }, { "filter", "a{sv}" }),
			GDBUS_ARGS({ "messages", "a{oa{sv}}" }),
			map_list_messages) },
	{ GDBUS_METHOD("ListFilterFields",
			NULL,
			GDBUS_ARGS({ "fields", "as" }),
			map_list_filter_fields) },
	{ GDBUS_ASYNC_METHOD("UpdateInbox",
			NULL,
			NULL,
			map_update_inbox) },
	{ }
};

static void map_msg_remove(void *data)
{
	struct map_msg *msg = data;
	char *path;

	path = msg->path;
	msg->path = NULL;
	g_dbus_unregister_interface(conn, path, MAP_MSG_INTERFACE);
	g_free(path);
}

static void map_free(void *data)
{
	struct map_data *map = data;

	obc_session_unref(map->session);
	g_hash_table_unref(map->messages);
	g_free(map);
}

static int map_probe(struct obc_session *session)
{
	struct map_data *map;
	const char *path;

	path = obc_session_get_path(session);

	DBG("%s", path);

	map = g_try_new0(struct map_data, 1);
	if (!map)
		return -ENOMEM;

	map->session = obc_session_ref(session);
	map->messages = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
								map_msg_remove);

	if (!g_dbus_register_interface(conn, path, MAP_INTERFACE, map_methods,
					NULL, NULL, map, map_free)) {
		map_free(map);

		return -ENOMEM;
	}

	return 0;
}

static void map_remove(struct obc_session *session)
{
	const char *path = obc_session_get_path(session);

	DBG("%s", path);

	g_dbus_unregister_interface(conn, path, MAP_INTERFACE);
}

static struct obc_driver map = {
	.service = "MAP",
	.uuid = MAS_UUID,
	.target = OBEX_MAS_UUID,
	.target_len = OBEX_MAS_UUID_LEN,
	.probe = map_probe,
	.remove = map_remove
};

int map_init(void)
{
	int err;

	DBG("");

	conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (!conn)
		return -EIO;

	err = obc_driver_register(&map);
	if (err < 0) {
		dbus_connection_unref(conn);
		conn = NULL;
		return err;
	}

	return 0;
}

void map_exit(void)
{
	DBG("");

	dbus_connection_unref(conn);
	conn = NULL;

	obc_driver_unregister(&map);
}
