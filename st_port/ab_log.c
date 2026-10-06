/* AmigaBridge log stub for the ST ports: AB_I/AB_W/AB_E lines to the
 * host via NatFeats as "<NAME> <level> <text>" (agent API /console). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "compat/bridge_client.h"
#include "natfeats.h"

static char prefix[16] = "AB";

int ab_init(const char *name)
{
	int i;
	for (i = 0; name[i] && i < 15; i++)
		prefix[i] = toupper((unsigned char)name[i]);
	prefix[i] = 0;
	return 0;
}

void ab_log(const char *level, const char *fmt, ...)
{
	char buf[200];
	int n;
	va_list ap;

	n = snprintf(buf, sizeof(buf), "%s %s ", prefix, level);
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	nf_print(buf);
}
