/* -------------------------------------------------------------------------
 * sound.c -- square-wave effects generated at boot.
 *
 * soundPlaySample() needs 16-bit PCM and the sample rate of that buffer.  The
 * buffers below are static (no malloc) and small (a few KiB each), which keeps
 * the ROM tiny and means the mixer never sees data that has been freed.
 * ------------------------------------------------------------------------- */
#include <string.h>

#include "sound.h"
#include "platform.h"

#ifndef HOST_TEST
#include <nds.h>
#endif

#define SND_RATE   16000
#define MAX_SAMPLES 4000            /* 250 ms at 16 kHz */

static s16 bufCorrect[MAX_SAMPLES];
static s16 bufWrong[MAX_SAMPLES];
static s16 bufGoal[MAX_SAMPLES];

static int lenCorrect, lenWrong, lenGoal;

/* Render a square wave that glides from f0 to f1 Hz.
 * `duty` is the fraction of the period spent high (in 1/16ths). */
static int synth(s16 *out, int maxSamples, int f0, int f1, int offsetSamples,
                 int ms, int duty, int volume)
{
    int n = SND_RATE * ms / 1000;
    if (offsetSamples + n > maxSamples) n = maxSamples - offsetSamples;
    if (n <= 0) return 0;

    float phase = 0.0f;
    int attack = n / 40 + 1;
    int release = n / 3 + 1;

    for (int i = 0; i < n; i++) {
        float k = (float)i / (float)n;
        int f = (int)((float)f0 + ((float)f1 - (float)f0) * k);
        if (f < 20) f = 20;
        phase += (float)f / (float)SND_RATE;
        while (phase >= 1.0f) phase -= 1.0f;

        int sample = (phase < (float)duty / 16.0f) ? 14000 : -14000;

        /* Soft attack and release so the speaker does not click. */
        int env = 255;
        if (i < attack) env = i * 255 / attack;
        int fromEnd = n - i;
        if (fromEnd < release) env = env * fromEnd / release;

        out[offsetSamples + i] = (s16)(sample * (env * volume / 255) / 255);
    }
    return n;
}

#ifndef HOST_TEST
static void play(const s16 *buf, int samples)
{
    if (samples <= 0) return;
    soundPlaySample(buf, SoundFormat_16Bit, (u32)(samples * 2), SND_RATE,
                    110, 64, false, 0);
}
#else
static void play(const s16 *buf, int samples) { (void)buf; (void)samples; }
#endif

void sndInit(void)
{
    /* correct: two quick rising notes */
    lenCorrect = synth(bufCorrect, MAX_SAMPLES, 880, 880, 0, 55, 8, 210);
    lenCorrect += synth(bufCorrect, MAX_SAMPLES, 1320, 1320, lenCorrect, 75, 8, 200);

    /* wrong: falling buzz */
    lenWrong = synth(bufWrong, MAX_SAMPLES, 240, 140, 0, 220, 6, 220);

    /* goal: three ascending notes */
    lenGoal = synth(bufGoal, MAX_SAMPLES, 660, 660, 0, 70, 8, 200);
    lenGoal += synth(bufGoal, MAX_SAMPLES, 880, 880, lenGoal, 70, 8, 200);
    lenGoal += synth(bufGoal, MAX_SAMPLES, 1320, 1320, lenGoal, 160, 8, 200);

#ifndef HOST_TEST
    soundEnable();
#endif
}

void sndCorrect(void) { play(bufCorrect, lenCorrect); }
void sndWrong(void)   { play(bufWrong, lenWrong); }
void sndGoal(void)    { play(bufGoal, lenGoal); }

void sndBuzzer(void)
{
    /* Reuse the wrong-answer buffer at a lower volume for the timeout buzz. */
    play(bufWrong, lenWrong);
}

void sndClick(void)
{
    /* 20 ms blip, rendered into the spare space at the end of bufGoal only if
     * there is room; otherwise the wrong-answer buzz is short enough to reuse. */
    static s16 blip[400];
    static int blipLen;
    if (!blipLen) blipLen = synth(blip, 400, 1800, 1800, 0, 20, 8, 120);
    play(blip, blipLen);
}
