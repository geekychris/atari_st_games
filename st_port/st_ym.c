/*
 * YM2149 PSG helpers, see st_ym.h.
 */
#include "st_ym.h"

#define YM_SELECT (*(volatile UBYTE *)0xffff8800)
#define YM_WRITE  (*(volatile UBYTE *)0xffff8802)

static UBYTE mixer_io;
static UWORD periods[128];

void ym_write(UBYTE reg, UBYTE val)
{
	YM_SELECT = reg;
	YM_WRITE = val;
}

static UBYTE ym_read(UBYTE reg)
{
	YM_SELECT = reg;
	return YM_SELECT;
}

void ym_init(void)
{
	float f = 440.0f;
	for (int n = 69; n < 128; n++, f *= 1.0594631f)
		periods[n] = (UWORD)(125000.0f / f + 0.5f);
	f = 440.0f;
	for (int n = 69; n >= 0; n--, f /= 1.0594631f)
		periods[n] = f > 31.0f ? (UWORD)(125000.0f / f + 0.5f) : 4095;
	mixer_io = ym_read(7) & 0xc0;
	ym_exit();
}

void ym_exit(void)
{
	for (int r = 8; r <= 10; r++)
		ym_write(r, 0);
	ym_write(7, mixer_io | 0x3f);
}

UWORD ym_period(int n)
{
	return periods[n & 127];
}

void ym_tone(int ch, UWORD period)
{
	ym_write(ch * 2, period & 0xff);
	ym_write(ch * 2 + 1, (period >> 8) & 0x0f);
}

void ym_note(int ch, int n)
{
	ym_tone(ch, periods[n & 127]);
}

void ym_noise(UBYTE period)
{
	ym_write(6, period & 31);
}

void ym_volume(int ch, int vol)
{
	ym_write(8 + ch, vol < 0 ? 0 : vol > 15 ? 15 : vol);
}

void ym_mix(int tone_mask, int noise_mask)
{
	ym_write(7, mixer_io | ((~tone_mask) & 7) | (((~noise_mask) & 7) << 3));
}
