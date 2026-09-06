#include "szs-sentry.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SENTRY_ENABLED
#include <sentry.h>

static bool sentry_initialized = false;

void szs_sentry_init(void)
{
	if (sentry_initialized)
		return;

	const char *dsn = getenv("SENTRY_DSN");
	if (!dsn || !*dsn)
		dsn = "https://a7c5633759c90496488fbcd6c13dee64@o107347.ingest.us.sentry.io/4512040243363840";

	sentry_options_t *options = sentry_options_new();
	sentry_options_set_dsn(options, dsn);
	sentry_options_set_database_path(options, ".sentry-native");

	char release_buf[128];
	snprintf(release_buf, sizeof(release_buf), "%s@%s",
		ProgInfo.progname && *ProgInfo.progname ? ProgInfo.progname : "wiimms-szs-tools-plus",
		VERSION);
	sentry_options_set_release(options, release_buf);

	const char *debug_env = getenv("SENTRY_DEBUG");
	if (debug_env && (*debug_env == '1' || *debug_env == 'y' || *debug_env == 'Y'))
		sentry_options_set_debug(options, 1);
	else
		sentry_options_set_debug(options, 0);

	sentry_init(options);
	sentry_initialized = true;
	atexit(szs_sentry_shutdown);
}

void szs_sentry_shutdown(void)
{
	if (!sentry_initialized)
		return;
	sentry_initialized = false;
	sentry_close();
}

void szs_sentry_capture_message(ccp message)
{
	if (!message || !*message)
		return;

	if (!sentry_initialized)
		szs_sentry_init();

	sentry_capture_event(sentry_value_new_message_event(
		SENTRY_LEVEL_INFO,
		"custom",
		message
	));
}

#else

void szs_sentry_init(void) {}
void szs_sentry_shutdown(void) {}
void szs_sentry_capture_message(ccp message) { (void)message; }

#endif
