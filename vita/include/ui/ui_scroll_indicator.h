/**
 * @file ui_scroll_indicator.h
 * @brief C24 ScrollIndicator: a thin track and thumb beside a scrolling pane (SPEC.md C24)
 *
 * Display-only. Shown only when there are more rows than fit. Paper cost: 2 draws (track, thumb).
 */

#pragma once

/**
 * ui_scroll_indicator_draw() - Draw the track and the thumb.
 * @x, @y:    Top-left corner of the track.
 * @h:        Track height (the pane's height).
 * @total:    Rows in the list.
 * @visible:  Rows that fit on screen.
 * @first:    Index of the first row on screen.
 *
 * Draws nothing when @total does not exceed @visible. The thumb is @visible/@total of the
 * track long and sits at @first/@total of its height.
 */
void ui_scroll_indicator_draw(int x, int y, int h, int total, int visible, int first);
