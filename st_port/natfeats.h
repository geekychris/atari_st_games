#ifndef NATFEATS_H
#define NATFEATS_H

/* returns non-zero when NatFeats (NF_STDERR) are available */
int nf_init(void);
/* print string on emulator host stderr, no-op without NatFeats */
void nf_print(const char *str);

#endif
