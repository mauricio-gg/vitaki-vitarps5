#pragma once

/*
 * GH #458: the debug "Input test" panel, drawn over the stream on the UI thread. It shows what the
 * input thread last SENT (front touches, click, motion) next to the raw Vita motion sample, from
 * debug_input_snapshot (debug_tools.h). Debug-tools builds only.
 */

#include "debug_tools.h"

#if VITARPS5_DEBUG_TOOLS

/* Draws the open panel at DEBUG_PANEL_RECT. UI thread only; the caller decides when it is shown. */
void debug_input_panel_draw(void);

/* Forgets the per-panel history (lifted-finger ring, cached text). Call on open, close, new stream.
 */
void debug_input_panel_reset(void);

#endif /* VITARPS5_DEBUG_TOOLS */
