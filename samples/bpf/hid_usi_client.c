// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Intel Corporation
 */

#include <stdio.h>
#include <gio/gio.h>
#include <getopt.h>

static void usi_var_get(GDBusProxy *proxy, const char *var)
{
	GVariant *result;
	unsigned int val;
	const GVariantType *type;
	char *str;

	result = g_dbus_proxy_get_cached_property(proxy, var);
	type = g_variant_get_type(result);
	if (g_variant_type_equal(type, G_VARIANT_TYPE_UINT32)) {
		g_variant_get(result, "u", &val);
		printf("Value for %s (u): %u\n", var, val);
	} else if (g_variant_type_equal(type, G_VARIANT_TYPE_STRING)) {
		g_variant_get(result, "&s", &str);
		printf("Value for %s (s): %s\n", var, str);
	} else {
		printf("Unsupported type %s for %s\n",
		       g_variant_get_type_string(result), var);
	}
	g_variant_unref(result);
}

static void usi_dump_vars(GDBusProxy *proxy)
{
	gchar **vars;
	int i;

	vars = g_dbus_proxy_get_cached_property_names(proxy);

	i = 0;
	while (vars && vars[i]) {
		usi_var_get(proxy, vars[i]);
		i++;
	}

	g_strfreev(vars);
}

static void usi_var_set(GDBusProxy *proxy, const char *var, unsigned int value)
{
	GError *error = NULL;

	g_dbus_proxy_call_sync(proxy,
			       "org.freedesktop.DBus.Properties.Set",
			       g_variant_new("(su)", var, value),
			       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

	g_assert_no_error(error);
}

static void usage(void)
{
	extern const char *__progname;

	printf("Usage: %s [--help] [--dump] [--color] [--width] [--style] [value]\n",
	       __progname);

	printf("\nOptions:\n");
	printf("    --help, -h          this help text\n");
	printf("    --color [value]     gets/sets stylus color\n");
	printf("    --width [value]     gets/sets stylus line width\n");
	printf("    --style [value]     gets/sets stylus line style\n");
	printf("    --dump              dump all variables\n");
}

int main(int argc, char *argv[])
{
	GDBusProxy *proxy;
	GDBusConnection *conn;
	GError *error = NULL;
	const char *version;
	GVariant *variant;
	const char *var = NULL;
	static struct option options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "color", no_argument, NULL, 'c' },
		{ "width", no_argument, NULL, 'w' },
		{ "style", no_argument, NULL, 's' },
		{ "dump", no_argument, NULL, 'd' },
	};
	int opt;
	int retval = 0;
	unsigned int value;

	conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	g_assert_no_error(error);

	proxy = g_dbus_proxy_new_sync(conn,
				      G_DBUS_PROXY_FLAGS_NONE,
				      NULL,				/* GDBusInterfaceInfo */
				      "org.universalstylus.PenServer",		/* name */
				      "/org/universalstylus/Pen",	/* object path */
				      "org.universalstylus.PenInterface",	/* interface */
				      NULL,				/* GCancellable */
				      &error);
	g_assert_no_error(error);

	/* read the version property of the interface */
	variant = g_dbus_proxy_get_cached_property(proxy, "Version");
	g_assert(variant != NULL);
	g_variant_get(variant, "s", &version);
	g_variant_unref(variant);
	printf("Server version v%s\n", version);

	while ((opt = getopt_long(argc, argv, "hcwsd", options, NULL)) != -1) {
		switch (opt) {
		case 'c':
			var = "LineColor";
			break;
		case 'w':
			var = "LineWidth";
			break;
		case 's':
			var = "LineStyle";
			break;
		case 'd':
			usi_dump_vars(proxy);
			goto exit;

		default:
			usage();
			retval = EXIT_FAILURE;
			goto exit;
		}
	}

	if (!var) {
		usage();
		retval = EXIT_FAILURE;
		goto exit;
	}
	if (argc == optind) {
		usi_var_get(proxy, var);
	} else if (argc == optind + 1) {
		value = atoi(argv[argc - 1]);
		usi_var_set(proxy, var, value);
	} else {
		usage();
		retval = EXIT_FAILURE;
	}

exit:
	g_object_unref(proxy);
	g_object_unref(conn);
	return retval;
}
