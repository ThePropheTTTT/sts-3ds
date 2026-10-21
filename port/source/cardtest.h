/* Card effect self-test (build.sh CARD_TEST=1): the assertions run at boot on
 * the 3DS itself and the report is drawn on the top screen, so a screenshot is
 * the evidence.  Not part of the shipping build. */
#pragma once

#include "common.h"

#if defined(CARD_TEST)
void card_test_run(void);
void card_test_draw(C3D_RenderTarget *top);
/* Shows one of the three picker layouts for a screenshot: 0 grid, 1 Discovery,
 * 2 a pile longer than one page. */
void card_test_demo_pick(int which);
/* Puts a real act 2 boss (0) or elite (1) on the battle screen so the new
 * intents and power icons can be screenshotted. */
void card_test_demo_fight(int which);
void card_test_demo_event(int which);
void card_test_demo_exit(void);
#endif
