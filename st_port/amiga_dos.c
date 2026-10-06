/*
 * AmigaDOS file calls on GEMDOS for the ST ports (see compat/amiga_compat.h).
 * Volume prefixes ("PROGDIR:", "S:", ...) are dropped, so files are
 * looked up in the current directory, and names are shortened to 8.3:
 * "orbital_patrol.mod" -> "ORBITAL_.MOD".
 */
#include <osbind.h>
#include <ctype.h>
#include "compat/amiga_compat.h"

static void dos_name(const char *in, char *out)
{
	const char *p = in, *dot;
	int n;

	for (const char *c = in; *c; c++)
		if (*c == ':' || *c == '/')
			p = c + 1;
	dot = strrchr(p, '.');
	for (n = 0; *p && p != dot && n < 8; p++)
		out[n++] = toupper((unsigned char)*p);
	if (dot)
	{
		int e = 0;
		out[n++] = '.';
		for (p = dot + 1; *p && e < 3; p++, e++)
			out[n++] = toupper((unsigned char)*p);
	}
	out[n] = 0;
}

BPTR Open(CONST_STRPTR name, LONG mode)
{
	char dn[16];
	long fh;

	dos_name(name, dn);
	if (mode == MODE_NEWFILE)
		fh = Fcreate(dn, 0);
	else
		fh = Fopen(dn, mode == MODE_READWRITE ? 2 : 0);
	return fh < 0 ? 0 : fh + 1;	/* 0 = failure, as on the Amiga */
}

LONG Close(BPTR fh)
{
	return fh ? Fclose((short)(fh - 1)) == 0 : 0;
}

LONG Read(BPTR fh, APTR buf, LONG len)
{
	return fh ? Fread((short)(fh - 1), len, buf) : -1;
}

LONG Write(BPTR fh, const void *buf, LONG len)
{
	return fh ? Fwrite((short)(fh - 1), len, (void *)buf) : -1;
}

/* returns the previous position, like AmigaDOS */
LONG Seek(BPTR fh, LONG pos, LONG mode)
{
	long old;
	if (!fh)
		return -1;
	old = Fseek(0, (short)(fh - 1), 1);
	if (Fseek(pos, (short)(fh - 1), mode == OFFSET_BEGINNING ? 0 : mode == OFFSET_END ? 2 : 1) < 0)
		return -1;
	return old;
}

void Delay(LONG ticks)
{
	/* _hz_200 counts at 200 Hz; readable from user mode via Supexec,
	 * the ports run in supervisor mode anyway */
	volatile long *hz200 = (volatile long *)0x4ba;
	long end = *hz200 + ticks * 4;
	while (*hz200 < end)
		;
}
