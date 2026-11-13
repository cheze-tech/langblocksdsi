/* -------------------------------------------------------------------------
 * save.c -- libfat persistence.
 *
 * On a DSi with an SD card libfat mounts the card as "sd:/"; on a DS flashcard
 * it is usually "fat:/".  We try a few device names and use the first one that
 * actually accepts a write, so a missing card only means "progress is not
 * saved this session" rather than a crash at boot.
 * ------------------------------------------------------------------------- */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "data.h"
#include "quiz.h"
#include "save.h"

/* Boot isolation switch: 0 keeps the profile in RAM only and never calls
 * libfat.  The Makefile also stops linking -lfat, so the ROM no longer gets
 * the 16 KB .dldi section that the known-good hello_world build lacks. */
#define SAVE_TO_SD 0

#if SAVE_TO_SD && !defined(HOST_TEST)
#include <fat.h>
#endif

SaveData save;

static char sPath[64];
static int sAvailable;
static int sDirty;

/* ------------------------------------------------------------------ misc */

#if SAVE_TO_SD
static u16 calcChecksum(const SaveData *d)
{
    const u8 *p = (const u8 *)d;
    const size_t skipStart = offsetof(SaveData, checksum);
    const size_t skipEnd = skipStart + sizeof(d->checksum);
    u16 sum = 0x1234;

    for (size_t i = 0; i < sizeof(SaveData); i++) {
        if (i >= skipStart && i < skipEnd) continue;
        sum = (u16)(sum + (u16)p[i] * (u16)(i + 3));
    }
    return sum;
}
#endif

void saveDefaults(void)
{
    memset(&save, 0, sizeof(save));      /* also zeroes padding, see checksum */
    save.magic = SAVE_MAGIC;
    save.version = SAVE_VERSION;
    save.dailyGoal = DAILY_GOAL;
    save.drillMask = QK_MASK_ALL;
    save.lang = 0;
    sDirty = 1;
}

int saveAvailable(void) { return sAvailable; }
const char *savePath(void) { return sPath; }

/* --------------------------------------------------------------- load/save */

#if SAVE_TO_SD
static void loadFile(void)
{
    FILE *f = fopen(sPath, "rb");
    if (!f) return;

    SaveData tmp;
    memset(&tmp, 0, sizeof(tmp));
    size_t got = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);

    if (got != sizeof(tmp)) return;                    /* short/empty file     */
    if (tmp.magic != SAVE_MAGIC) return;
    if (tmp.version != SAVE_VERSION) return;
    if (tmp.checksum != calcChecksum(&tmp)) return;    /* corrupted            */

    save = tmp;

    /* Defensive clamps: a hand-edited file must not produce silly numbers. */
    if (save.gameMs > MAX_BANK_MS) save.gameMs = MAX_BANK_MS;
    if (save.dailyGoal == 0) save.dailyGoal = DAILY_GOAL;
    if (save.lang >= LANG_COUNT) save.lang = 0;
    save.drillMask &= QK_MASK_ALL;
    if (save.drillMask == 0) save.drillMask = QK_MASK_ALL;
}
#endif

void saveNow(void)
{
#if !SAVE_TO_SD
    /* RAM-only build: nothing to flush. */
    sDirty = 0;
    return;
#else
    if (!sAvailable) {
        sDirty = 0;
        return;
    }

    save.magic = SAVE_MAGIC;
    save.version = SAVE_VERSION;
    save.checksum = calcChecksum(&save);

    char tmpPath[sizeof(sPath) + 8];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", sPath);

    FILE *f = fopen(tmpPath, "wb");
    if (!f) return;
    size_t wrote = fwrite(&save, 1, sizeof(save), f);
    int closed = fclose(f);
    if (wrote != sizeof(save) || closed != 0) {
        remove(tmpPath);
        return;
    }

    /* libfat has no atomic replace, so: delete the old file, then rename.  If
     * power is lost between the two the .tmp file is still there and the next
     * boot simply starts from defaults. */
    remove(sPath);
    if (rename(tmpPath, sPath) != 0) {
        remove(tmpPath);
        return;
    }
    sDirty = 0;
#endif
}

/* ------------------------------------------------------------------ setup */

void saveInit(void)
{
    saveDefaults();
    sAvailable = 0;
    sPath[0] = '\0';

#if SAVE_TO_SD && !defined(HOST_TEST)
    /* "sd:/" is the DSi SD slot, "fat:/" the DLDI device of a DS flashcard.
     * Both are tried; if neither can be opened the game still runs, it just
     * does not persist progress. */
    static const char *devices[] = { "sd:/", "fat:/" };
    static const char *filename = "langblocks.sav";

    if (fatInitDefault()) {
        for (unsigned int i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
            char candidate[64];
            snprintf(candidate, sizeof(candidate), "%s%s", devices[i], filename);

            /* Opening for append creates the file if the device is writable,
             * which is a harmless way to test the mount. */
            FILE *f = fopen(candidate, "ab");
            if (!f) continue;

            fclose(f);
            strncpy(sPath, candidate, sizeof(sPath) - 1);
            sPath[sizeof(sPath) - 1] = '\0';
            sAvailable = 1;
            break;
        }
    }

    if (sAvailable) loadFile();
#endif
}

void saveNewSession(void)
{
    /* Rule: a "day" is one session (libnds only exposes the RTC to the ARM7
     * core).  If the previous session reached the daily goal the streak
     * continues; otherwise it resets. */
    if (!save.dailyDone) save.streak = 0;
    save.dailyDone = 0;
    save.dailyCorrect = 0;
    if (save.dailyGoal == 0) save.dailyGoal = DAILY_GOAL;
    sDirty = 1;
}
