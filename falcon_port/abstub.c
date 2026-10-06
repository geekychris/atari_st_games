/* AmigaBridge log stub: AB_I/AB_W/AB_E lines to the host via NatFeats,
 * prefixed "FRACTALUS <level> " (agent API /console). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bridge_client.h"

void ab_log(const char *level, const char *fmt, ...)
{
	char buf[200];
	int n;
	va_list ap;

	n = snprintf(buf, sizeof(buf), "FRACTALUS %s ", level);
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	nf_print(buf);
}
