/* snd_ps2.c: the sound driver (snd_sys.c's snddrv_ps2). uHexen2's mixer
   (snd_dma.c, snd_mix.c) paints at 22050 Hz 16-bit stereo into a ring here,
   and from it into maxsdk's PCM stream (mx/snd.h: audsrv, which plays it
   at 48 kHz). Submit sends what the mixer has painted since the last call
   (up to paintedtime), as far as the stream has room; GetDMAPos reports
   what has been sent as played. The sound plays the stream's ring later
   (about 100 ms). As apps/quake1's snd_ps2.c. */

#include <mx/snd.h>

#include "quakedef.h"
#include "snd_sys.h"

#define RATE 22050
#define FRAMES 8192 /* the ring: 0.37 s, a power of two */

static s16 ring[FRAMES * 2] __attribute__((aligned(64)));
static int sent; /* frames sent to the stream, as paintedtime counts them */

static qboolean S_PS2_Init(dma_t *dma)
{
	if (mx_snd_init() < 0 || mx_snd_stream_format(RATE, 16, 2) < 0) {
		Con_Printf("S_PS2_Init: no audsrv\n");
		return false;
	}
	memset(dma, 0, sizeof(dma_t));
	shm = dma;
	shm->channels = 2;
	shm->samplebits = 16;
	shm->speed = RATE;
	shm->samples = FRAMES * 2;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *)ring;
	sent = 0;
	Con_Printf("Sound: %d Hz 16-bit stereo through audsrv\n", RATE);
	return true;
}

static int S_PS2_GetDMAPos(void)
{
	shm->samplepos = (sent * 2) & (FRAMES * 2 - 1);
	return shm->samplepos;
}

static void S_PS2_Submit(void)
{
	if (paintedtime < sent) /* snd_dma.c chopped its counters */
		sent = paintedtime;
	int n = paintedtime - sent;
	int room = (int)(mx_snd_stream_room() / 4);
	if (n > room)
		n = room;
	while (n > 0) {
		int at = sent & (FRAMES - 1);
		int k = FRAMES - at < n ? FRAMES - at : n;
		int took = (int)(mx_snd_stream_queue(ring + at * 2, (u32)k * 4) / 4);
		sent += took;
		n -= took;
		if (took < k)
			break;
	}
}

static void S_PS2_Shutdown(void)
{
	mx_snd_stream_stop();
}

static void S_PS2_Nothing(void)
{
}

snd_driver_t snddrv_ps2 = {
	S_PS2_Init,
	S_PS2_Shutdown,
	S_PS2_GetDMAPos,
	S_PS2_Nothing, /* LockBuffer */
	S_PS2_Submit,
	S_PS2_Nothing, /* BlockSound */
	S_PS2_Nothing, /* UnblockSound */
	"PS2 audsrv",
	SNDDRV_ID_PS2,
	false,
	NULL
};
