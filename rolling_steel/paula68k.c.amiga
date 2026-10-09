/*
 * The real Paula under the 3DO ports' sound code: see paula.h. The same
 * file is in planet_chomp, rolling_steel and spectral_keep.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/audio.h>
#include <proto/exec.h>
#include "paula.h"

static struct MsgPort *port;
static struct IOAudio *ioa;
static int open_;
static void (*tick_fn)(void);

int paula_open(void)
{
    static UBYTE all[1] = { 15 };           /* channels 0..3 */
    port = CreateMsgPort();
    if (!port) return 0;
    ioa = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio));
    if (!ioa) return 0;
    ioa->ioa_Request.io_Message.mn_Node.ln_Pri = 127;
    ioa->ioa_Data = all;
    ioa->ioa_Length = sizeof(all);
    if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)ioa, 0) != 0) return 0;
    open_ = 1;
    custom.dmacon = DMAF_AUDIO;             /* all four off */
    custom.aud[0].ac_vol = custom.aud[1].ac_vol = custom.aud[2].ac_vol = custom.aud[3].ac_vol = 0;
    return 1;
}

void paula_close(void)
{
    if (open_) {
        custom.dmacon = DMAF_AUDIO;
        CloseDevice((struct IORequest *)ioa);
    }
    open_ = 0;
    if (ioa) DeleteIORequest((struct IORequest *)ioa);
    if (port) DeleteMsgPort(port);
    ioa = 0;
    port = 0;
}

void  paula_dmacon(UWORD v) { if (open_) custom.dmacon = v; }
void  paula_set_tick(void (*tick)(void)) { tick_fn = tick; }
void  paula_tick(void) { if (tick_fn && open_) tick_fn(); }
UWORD paula_lock(void) { return 0; }
void  paula_unlock(UWORD sr) { (void)sr; }
