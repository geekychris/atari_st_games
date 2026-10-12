// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* AmigaBridge log stub: AB_I/AB_W/AB_E lines to the host via NatFeats,
 * prefixed "<NAME> <level> " (agent API /console): CANYON unless
 * the program called ab_init("NAME"). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bridge_client.h"

static char ab_name[16] = "CANYON";

int ab_init(const char *name)
{
	if (name && *name) {
		strncpy(ab_name, name, sizeof(ab_name) - 1);
		ab_name[sizeof(ab_name) - 1] = 0;
	}
	return 0;
}

void ab_log(const char *level, const char *fmt, ...)
{
	char buf[200];
	int n;
	va_list ap;

	n = snprintf(buf, sizeof(buf), "%s %s ", ab_name, level);
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	nf_print(buf);
}
