/*
 * Toolchain smoke test for the StS -> 3DS port.
 *
 * It deliberately draws the shape of a combat screen (a hand of five cards, an
 * energy orb, one enemy block) with citro2d, and keeps the libctru console on
 * the bottom screen for live stats.  If this builds and boots, the toolchain is
 * good and we can start replacing the placeholders with the real art.
 */

#include <citro2d.h>

#include <stdio.h>
#include <string.h>

#define TOP_W 400
#define TOP_H 240

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    consoleInit(GFX_BOTTOM, NULL);

    /* C2D_Color32 is an inline function, not a constant expression, so the
     * palette has to live in locals rather than at file scope. */
    const u32 CLR_BG     = C2D_Color32(0x14, 0x16, 0x1C, 0xFF);
    const u32 CLR_CARD   = C2D_Color32(0x6E, 0x52, 0x2A, 0xFF);
    const u32 CLR_ART    = C2D_Color32(0x8B, 0x2F, 0x2F, 0xFF);
    const u32 CLR_ENERGY = C2D_Color32(0xE8, 0x6A, 0x1E, 0xFF);
    const u32 CLR_ENEMY  = C2D_Color32(0x4A, 0x2F, 0x5D, 0xFF);
    const u32 CLR_HP     = C2D_Color32(0xC8, 0x2A, 0x2A, 0xFF);
    const u32 CLR_HP_BG  = C2D_Color32(0x2A, 0x12, 0x12, 0xFF);

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    unsigned long frame = 0;
    int hand = 5;

    printf("\x1b[1;1H  StS -> 3DS  \xc2\xb7  toolchain check");
    printf("\x1b[2;1H  armv6k / citro2d / libctru");

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        if (kDown & KEY_A) {
            hand = 5;
            frame = 0;
        }
        if (kDown & KEY_B)
            hand = hand > 1 ? hand - 1 : 1;
        if (kDown & KEY_X)
            hand = hand < 5 ? hand + 1 : 5;

        printf("\x1b[4;1Hframe   %9lu", frame);
        printf("\x1b[5;1HCPU     %8.1f%%", C3D_GetProcessingTime() * 6.0f);
        printf("\x1b[6;1HGPU     %8.1f%%", C3D_GetDrawingTime() * 6.0f);
        printf("\x1b[7;1HCmdBuf  %8.1f%%", C3D_GetCmdBufUsage() * 100.0f);
        printf("\x1b[9;1Hcards on screen: %d", hand);
        printf("\x1b[11;1HA: reset   B: fewer cards");
        printf("\x1b[12;1HX: more cards   START: exit");

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, CLR_BG);
        C2D_SceneBegin(top);

        /* hand of cards, bottom of the top screen */
        for (int i = 0; i < hand; i++) {
            float x = 40.0f + i * 66.0f;
            C2D_DrawRectSolid(x, 148.0f, 0.0f, 60.0f, 86.0f, CLR_CARD);
            C2D_DrawRectSolid(x + 3.0f, 151.0f, 0.0f, 54.0f, 40.0f, CLR_ART);
        }

        /* energy orb, top-left */
        C2D_DrawCircleSolid(32.0f, 32.0f, 0.0f, 19.0f, CLR_ENERGY);

        /* one enemy with an hp bar */
        C2D_DrawRectSolid(300.0f, 40.0f, 0.0f, 76.0f, 76.0f, CLR_ENEMY);
        C2D_DrawRectSolid(292.0f, 122.0f, 0.0f, 92.0f, 8.0f, CLR_HP_BG);
        C2D_DrawRectSolid(292.0f, 122.0f, 0.0f, 72.0f, 8.0f, CLR_HP);

        C3D_FrameEnd(0);

        frame++;
    }

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
