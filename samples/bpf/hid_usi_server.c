// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Intel Corporation
 */
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/resource.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib.h>

#include "bpf_util.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <sys/stat.h>

#include "hid_usi.h"

static const char *version = "0.5";
static GMainLoop *mainloop;

static char *sysfs_path;
static int sysfs_fd;
static int prog_count;
static int cache, wr_cache;

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
};

struct prog {
	int fd;
	enum bpf_attach_type type;
};

static struct prog progs[10];

static const char *server_introspection_xml =
	DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
	"<node>\n"
	"  <interface name='org.freedesktop.DBus.Introspectable'>\n"
	"    <method name='Introspect'>\n"
	"      <arg name='data' type='s' direction='out' />\n"
	"    </method>\n"
	"  </interface>\n"

	"  <interface name='org.freedesktop.DBus.Properties'>\n"
	"    <method name='Get'>\n"
	"      <arg name='property'  type='s' direction='in' />\n"
	"      <arg name='value'     type='s' direction='out' />\n"
	"    </method>\n"
	"    <method name='Set'>\n"
	"      <arg name='property'  type='s' direction='in' />\n"
	"      <arg name='value'     type='u' direction='in' />\n"
	"    </method>\n"
	"    <method name='GetAll'>\n"
	"      <arg name='properties' type='a{sv}' direction='out'/>\n"
	"    </method>\n"
	"  </interface>\n"

	"  <interface name='org.universalstylus.PenInterface'>\n"
	"    <property name='Version' type='s' access='read' />\n"
	"    <property name='LineColor' type='u' access='readwrite' />\n"
	"    <property name='LineWidth' type='u' access='readwrite' />\n"
	"    <property name='LineStyle' type='u' access='readwrite' />\n"
	"  </interface>\n"
	"</node>\n";

static void int_exit(int sig)
{
	int ret;

	for (prog_count--; prog_count >= 0 ; prog_count--) {
		ret = bpf_prog_detach2(progs[prog_count].fd, sysfs_fd, progs[prog_count].type);
		if (ret)
			fprintf(stderr, "bpf_prog_detach2: returned %m\n");
	}

	close(sysfs_fd);
	exit(0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s /sys/bus/hid/devices/<dev>/modalias\n\n",
		prog);
}

static int param_to_idx(const char *param)
{
	if (!strcmp(param, "LineColor"))
		return USI_PEN_COLOR;
	if (!strcmp(param, "LineWidth"))
		return USI_PEN_LINE_WIDTH;
	if (!strcmp(param, "LineStyle"))
		return USI_PEN_LINE_STYLE;

	return -EINVAL;
}

static int write_value(const char *param, int value)
{
	int err;
	int idx = param_to_idx(param);

	printf("%s: param=%s (%d), value=%d\n", __func__, param, idx, value);
	err = bpf_map_update_elem(wr_cache, &idx, &value, BPF_ANY);
	if (err) {
		printf("Update failed for %d, err=%d\n", idx, err);
		return err;
	}

	return 0;
}

static int read_value(const char *param)
{
	int value = -ENOENT;
	int idx = param_to_idx(param);

	bpf_map_lookup_elem(cache, &idx, &value);

	return value;
}

static DBusHandlerResult usi_set_prop(DBusConnection *conn, DBusError *err,
				      DBusMessage *msg, DBusMessage *reply)
{
	const char *property;
	unsigned int value;
	int ret;

	if (!dbus_message_get_args(msg, err,
				   DBUS_TYPE_STRING, &property,
				   DBUS_TYPE_UINT32, &value,
				   DBUS_TYPE_INVALID))
		return -1;

	if (write_value(property, value) < 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult usi_get_prop(DBusConnection *conn, DBusError *err,
				      DBusMessage *msg, DBusMessage *reply)
{
	const char *property;
	int value;

	if (!dbus_message_get_args(msg, err,
				   DBUS_TYPE_STRING, &property,
				   DBUS_TYPE_INVALID))
		return -1;

	if (!strcmp(property, "Version")) {
		dbus_message_append_args(reply,
					 DBUS_TYPE_STRING, &version,
					 DBUS_TYPE_INVALID);
	} else {
		value = read_value(property);
		if (value < 0)
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		dbus_message_append_args(reply,
					 DBUS_TYPE_UINT32, &value,
					 DBUS_TYPE_INVALID);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult usi_get_all_props(DBusConnection *conn, DBusError *err,
					   DBusMessage *reply)
{
	DBusMessageIter array, dict, iter, variant;
	const char *props[] = {
		"Version", "LineColor", "LineWidth", "LineStyle"
	};
	const char *types[] = { "s", "u", "u", "u" };
	int i;
	unsigned int value;
	void *ptr;
	int itype;

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict);
		dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &props[i]);
		dbus_message_iter_open_container(&dict, DBUS_TYPE_VARIANT, types[i], &variant);
		if (i == 0) {
			ptr = &version;
			itype = DBUS_TYPE_STRING;
		} else {
			value = read_value(props[i]);
			ptr = &value;
			itype = DBUS_TYPE_UINT32;
		}
		dbus_message_iter_append_basic(&variant, itype, ptr);
		dbus_message_iter_close_container(&dict, &variant);
		dbus_message_iter_close_container(&array, &dict);
	}

	dbus_message_iter_close_container(&iter, &array);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult usi_message_handler(DBusConnection *conn,
					     DBusMessage *message, void *data)
{
	DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	DBusMessage *reply;
	DBusError err;
	int value;

	fprintf(stderr, "Got D-Bus request: %s.%s on %s\n",
		dbus_message_get_interface(message),
		dbus_message_get_member(message),
		dbus_message_get_path(message));

	dbus_error_init(&err);

	reply = dbus_message_new_method_return(message);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	if (dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE, "Introspect")) {
		dbus_message_append_args(reply,
					 DBUS_TYPE_STRING, &server_introspection_xml,
					 DBUS_TYPE_INVALID);

	} else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Get")) {
		result = usi_get_prop(conn, &err, message, reply);
	} else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "GetAll")) {
		result = usi_get_all_props(conn, &err, reply);
	} else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Set")) {
		result = usi_set_prop(conn, &err, message, reply);
	} else {
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (dbus_error_is_set(&err)) {
		dbus_message_unref(reply);

		reply = dbus_message_new_error(message, err.name, err.message);
		dbus_error_free(&err);

		if (!reply)
			return DBUS_HANDLER_RESULT_NEED_MEMORY;
	}

	result = DBUS_HANDLER_RESULT_HANDLED;

	if (!dbus_connection_send(conn, reply, NULL))
		result = DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_unref(reply);

	return result;
}

const DBusObjectPathVTable usi_vtable = {
	.message_function = usi_message_handler,
};

static int server_run(void)
{
	DBusConnection *conn;
	DBusError err;
	int rv;

	dbus_error_init(&err);

	conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!conn) {
		fprintf(stderr, "Failed to get a session DBus connection: %s\n", err.message);
		goto fail;
	}

	rv = dbus_bus_request_name(conn, "org.universalstylus.PenServer",
				   DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
	if (rv != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		fprintf(stderr, "Failed to request name on bus: %s\n", err.message);
		goto fail;
	}

	if (!dbus_connection_register_object_path(conn,
						  "/org/universalstylus/Pen",
						  &usi_vtable, NULL)) {
		fprintf(stderr, "Failed to register a object path for 'Pen'\n");
		goto fail;
	}

	printf("Starting dbus USI server v%s\n", version);
	mainloop = g_main_loop_new(NULL, false);
	dbus_connection_setup_with_g_main(conn, NULL);
	g_main_loop_run(mainloop);

	return 0;
fail:
	dbus_error_free(&err);
	return -1;
}

static int attach_progs(int argc, char **argv)
{
	char filename[256];
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	struct bpf_object *obj;
	struct bpf_program *prog;
	int err = 0;
	char buf[BUFSIZ];
	char param[16];
	char op[8];
	int value;
	int m, n;
	struct sockaddr_un addr;
	struct sockaddr_un from;
	socklen_t from_len = sizeof(from);

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	err = libbpf_get_error(obj);
	if (err) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		obj = NULL;
		err = 1;
		goto cleanup;
	}

	/* load BPF program */
	err = bpf_object__load(obj);
	if (err) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	sysfs_fd = open(sysfs_path, O_RDONLY);

	bpf_object__for_each_program(prog, obj) {
		progs[prog_count].fd = bpf_program__fd(prog);
		progs[prog_count].type = bpf_program__get_expected_attach_type(prog);

		err = bpf_prog_attach(progs[prog_count].fd,
				      sysfs_fd,
				      progs[prog_count].type,
				      0);
		if (err) {
			fprintf(stderr, "bpf_prog_attach: err=%m\n");
			progs[prog_count].fd = 0;
			goto cleanup;
		}
		printf("attached BPF program with FD=%d, type=%d\n",
		       progs[prog_count].fd, progs[prog_count].type);
		prog_count++;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	err = bpf_obj_get_info_by_fd(progs[0].fd, &info, &info_len);
	if (err) {
		printf("can't get prog info - %s\n", strerror(errno));
		goto cleanup;
	}

	cache = bpf_object__find_map_fd_by_name(obj, "cache");
	if (cache < 0) {
		printf("can't get 'cache' shared mem from object - %m\n");
		goto cleanup;
	}

	wr_cache = bpf_object__find_map_fd_by_name(obj, "wr_cache");
	if (wr_cache < 0) {
		printf("can't get 'wr_cache' shared mem from object - %m\n");
		goto cleanup;
	}

	server_run();

	return 0;

 cleanup:
	for (prog_count--; prog_count >= 0; prog_count--) {
		if (bpf_prog_detach2(progs[prog_count].fd, sysfs_fd, progs[prog_count].type))
			fprintf(stderr, "bpf_prog_detach2: returned %m\n");
	}

	bpf_object__close(obj);
	return err;
}

int main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt_long(argc, argv, "h", long_options,
				  NULL)) != -1) {
		switch (opt) {
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (optind == argc) {
		usage(basename(argv[0]));
		return 1;
	}

	sysfs_path = argv[optind];
	if (!sysfs_path) {
		perror("hidraw");
		return 1;
	}

	printf("sysfs_path: %s\n", sysfs_path);

	return attach_progs(argc, argv);
}
