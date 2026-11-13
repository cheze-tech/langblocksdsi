/* -------------------------------------------------------------------------
 * sound.h -- tiny synthesised sound effects.
 *
 * No audio files and no maxmod soundbank: the three effects are square-wave
 * PCM buffers generated once at startup.  That keeps the build free of binary
 * assets while still giving the quiz audible feedback (the request asks for a
 * buzzer on a timeout).
 * ------------------------------------------------------------------------- */
#ifndef SOUND_H
#define SOUND_H

void sndInit(void);     /* build the buffers and enable the sound hardware */
void sndCorrect(void);  /* short rising blip                              */
void sndWrong(void);    /* descending buzz                                */
void sndBuzzer(void);   /* timeout buzzer (harsher and longer)            */
void sndGoal(void);     /* little fanfare when the daily goal is reached  */
void sndClick(void);    /* UI tick                                        */

#endif /* SOUND_H */
