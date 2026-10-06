/*
 * Minimal NatFeats client: lets a program print to the emulator host
 * (Hatari --natfeats on, ARAnyM).  Detection is safe on real hardware:
 * the NatFeats opcodes are illegal instructions there, which we catch.
 */
#include <osbind.h>
#include "natfeats.h"

long nf_get_id(const char *name);
long nf_call(long id, ...);

/* NatFeats are called like C functions: arguments on the stack */
__asm__(
	"	.text\n"
	"	.globl	nf_get_id\n"
	"nf_get_id:\n"
	"	.dc.w	0x7300\n"	/* NATFEAT_ID */
	"	rts\n"
	"	.globl	nf_call\n"
	"nf_call:\n"
	"	.dc.w	0x7301\n"	/* NATFEAT_CALL */
	"	rts\n"
	/* illegal instruction handler used during detection */
	"nf_illegal:\n"
	"	moveq	#0,%d0\n"
	"	addq.l	#2,2(%sp)\n"	/* skip the 2-byte NatFeats opcode */
	"	rte\n"
	"	.globl	nf_illegal_addr\n"
	"nf_illegal_addr:\n"
	"	.dc.l	nf_illegal\n"
);

extern void *nf_illegal_addr;

static long nf_stderr_id;

/* runs in supervisor mode (Supexec) */
static long detect(void)
{
	void **vector = (void **)0x10;	/* illegal instruction */
	void *old = *vector;

	*vector = nf_illegal_addr;
	nf_stderr_id = nf_get_id("NF_STDERR");
	*vector = old;
	return 0;
}

int nf_init(void)
{
	Supexec(detect);
	return nf_stderr_id != 0;
}

void nf_print(const char *str)
{
	if (nf_stderr_id)
		nf_call(nf_stderr_id, str);
}
