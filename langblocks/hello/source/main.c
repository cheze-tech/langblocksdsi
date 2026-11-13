/* -------------------------------------------------------------------------
 * hello -- the smallest possible libnds v2/calico ROM.
 *
 * consoleDemoInit() sets up the sub-engine text background; no bitmap VRAM
 * setup, no 3D, no sound, no FAT, no game data.  If this ROM does not show
 * HELLO WORLD then nothing in the game can work and the build/loader must be
 * fixed first.
 * ------------------------------------------------------------------------- */
#include <nds.h>
#include <stdio.h>

int main(void)
{
    consoleDemoInit();          /* sub engine, text BG0, 2D only */

    iprintf("\n\n");
    iprintf("      HELLO WORLD\n\n");
    iprintf("   libnds / calico test\n");
    iprintf("   press START to exit\n");

    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) break;
    }

    return 0;
}
