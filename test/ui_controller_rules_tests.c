// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Controller page rules in vita/src/ui/ui_controller_rules.c
// (ticket #305): what touching a zone really does, the whole-side value or Mixed, and the
// assignments that keep the stored map equal to what the page shows. The seeds come from the real
// vita/src/controller.c. `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=gnu99 -Wall -Wextra -I test/stubs -I vita/include -I lib/include \
//      test/ui_controller_rules_tests.c vita/src/ui/ui_controller_rules.c vita/src/controller.c \
//      -o /tmp/ui_controller_rules_tests && /tmp/ui_controller_rules_tests

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "context.h"
#include "controller.h"
#include "ui/ui_controller_rules.h"

VitaChiakiContext context;

enum {
  OUT_NONE = VITAKI_CTRL_OUT_NONE,
  OUT_L2 = VITAKI_CTRL_OUT_L2,
  OUT_TOUCHPAD = VITAKI_CTRL_OUT_TOUCHPAD,
};

/** The first-run map of a custom preset, from the real seeds. */
static ControllerMapStorage seeds(void) {
  ControllerMapStorage map;
  controller_map_storage_set_defaults(&map);
  return map;
}

static int front_cell(int zone) {
  return VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + zone;
}

static int rear_cell(int zone) {
  return VITAKI_CTRL_IN_REARTOUCH_GRID_START + zone;
}

/**
 * Zones that all hold one output read as that output, and a set that is all None reads as None
 * (the None row is ticked), not as Mixed.
 * catches: the whole-surface popup showing "Mixed" and ticking nothing for a side that is
 * uniformly mapped or uniformly cleared.
 */
static void test_uniform_set_has_a_common_output(void) {
  const int touchpad[] = {OUT_TOUCHPAD, OUT_TOUCHPAD, OUT_TOUCHPAD};
  assert(ui_controller_common_output(touchpad, 3) == OUT_TOUCHPAD);

  const int cleared[] = {OUT_NONE, OUT_NONE};
  assert(ui_controller_common_output(cleared, 2) == OUT_NONE);

  const int single[] = {OUT_L2};
  assert(ui_controller_common_output(single, 1) == OUT_L2);
}

/**
 * One zone that differs, wherever it sits in the set, makes the set Mixed.
 * catches: a set whose first zones agree being ticked as if all were the same (the popup would
 * then show a value that only some zones have), or a lone zone with None among mapped ones being
 * shown as the mapped value.
 */
static void test_any_difference_is_mixed(void) {
  const int last_differs[] = {OUT_L2, OUT_L2, OUT_NONE};
  assert(ui_controller_common_output(last_differs, 3) == UI_CTRL_MIXED);

  const int first_differs[] = {OUT_NONE, OUT_L2, OUT_L2};
  assert(ui_controller_common_output(first_differs, 3) == UI_CTRL_MIXED);

  const int middle_differs[] = {OUT_L2, OUT_TOUCHPAD, OUT_L2};
  assert(ui_controller_common_output(middle_differs, 3) == UI_CTRL_MIXED);
}

/**
 * An empty or missing set has no common output and must not read past its end.
 * catches: a crash or a bogus tick when the popup is opened on a selection that turned out empty.
 */
static void test_empty_set_is_mixed(void) {
  const int none[] = {OUT_NONE};
  assert(ui_controller_common_output(none, 0) == UI_CTRL_MIXED);
  assert(ui_controller_common_output(NULL, 3) == UI_CTRL_MIXED);
}

/**
 * With the real first-run seeds the whole front surface is Touchpad (the whole-surface input holds
 * it, the 18 front cells are empty) and the rear does nothing: its L2 and R2 sit on the legacy
 * quadrant inputs, which host_input.c never reads for a rear touch.
 * catches: the front reading "None, 0 zones" because only the 18 cells were looked at, or the
 * rear claiming L2/R2 zones that the stream never fires.
 */
static void test_seeds_front_is_touchpad_rear_does_nothing(void) {
  const ControllerMapStorage map = seeds();
  assert(map.in_out_btn[front_cell(0)] == OUT_NONE);
  assert(!vitaki_ctrl_in_is_rear_grid((VitakiCtrlIn)map.in_l2));

  assert(ui_controller_side_output(&map, UI_CTRL_SIDE_FRONT) == OUT_TOUCHPAD);
  assert(ui_controller_mapped_zones(&map, UI_CTRL_SIDE_FRONT) == UI_CTRL_ZONES);
  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_FRONT, 4) == OUT_TOUCHPAD);

  assert(ui_controller_side_output(&map, UI_CTRL_SIDE_REAR) == OUT_NONE);
  assert(ui_controller_mapped_zones(&map, UI_CTRL_SIDE_REAR) == 0);
}

/**
 * A cell that disagrees with the whole-surface input fires both, which one output cannot name:
 * it reads Mixed, and so does the side. A cell that holds the same output as the whole-surface
 * input is that output. A trigger on the whole-surface input presses the wrong buttons in the
 * stream, so it is Mixed everywhere.
 * catches: a cell showing "L1" while the stream also presses Touchpad there, or a side showing
 * Touchpad with a cell that does something else.
 */
static void test_zone_disagreeing_with_whole_surface_is_mixed(void) {
  ControllerMapStorage map = seeds();
  map.in_out_btn[front_cell(5)] = VITAKI_CTRL_OUT_L1;
  map.in_out_btn[front_cell(2)] = VITAKI_CTRL_OUT_TOUCHPAD;

  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_FRONT, 5) == UI_CTRL_MIXED);
  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_FRONT, 2) == OUT_TOUCHPAD);
  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_FRONT, 0) == OUT_TOUCHPAD);
  assert(ui_controller_side_output(&map, UI_CTRL_SIDE_FRONT) == UI_CTRL_MIXED);
  assert(ui_controller_mapped_zones(&map, UI_CTRL_SIDE_FRONT) == UI_CTRL_ZONES);

  const int selection[] = {0, 2};
  assert(ui_controller_zones_output(&map, UI_CTRL_SIDE_FRONT, selection, 2) == OUT_TOUCHPAD);

  ControllerMapStorage trigger = seeds();
  trigger.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_ANY] = VITAKI_CTRL_OUT_R2;
  assert(ui_controller_zone_output(&trigger, UI_CTRL_SIDE_REAR, 0) == UI_CTRL_MIXED);
}

/**
 * Assigning the whole side reads back as that value, empties the whole-surface input and leaves
 * the other side alone; assigning None (Clear) empties the front even though the seeds hold
 * Touchpad on the whole-surface input.
 * catches: Clear leaving the front on Touchpad, a whole-side choice that reads back as something
 * else, or one side's assignment spilling into the other.
 */
static void test_assigning_the_whole_side_reads_back(void) {
  const VitakiCtrlOut choices[] = {VITAKI_CTRL_OUT_L1, VITAKI_CTRL_OUT_NONE,
                                   VITAKI_CTRL_OUT_TOUCHPAD, VITAKI_CTRL_OUT_L2};
  for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); i++) {
    ControllerMapStorage map = seeds();
    ui_controller_assign_side(&map, UI_CTRL_SIDE_FRONT, choices[i]);
    assert(ui_controller_side_output(&map, UI_CTRL_SIDE_FRONT) == (int)choices[i]);
    assert(map.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY] == OUT_NONE);
    assert(ui_controller_side_output(&map, UI_CTRL_SIDE_REAR) == OUT_NONE);
  }

  ControllerMapStorage cleared = seeds();
  ui_controller_assign_side(&cleared, UI_CTRL_SIDE_FRONT, VITAKI_CTRL_OUT_NONE);
  assert(ui_controller_mapped_zones(&cleared, UI_CTRL_SIDE_FRONT) == 0);
}

/**
 * Assigning one zone while the whole-surface input still holds an output keeps the stream equal
 * to the page: the whole-surface output moves into the other 17 zones and the input is emptied, so
 * no hidden second output fires in the assigned zone.
 * catches: the zone labelled "L1" also pressing Touchpad, or the other 17 zones going dead when
 * the whole-surface input is emptied.
 */
static void test_assigning_one_zone_keeps_the_page_equal_to_the_stream(void) {
  ControllerMapStorage map = seeds();
  const int zone = 7;
  ui_controller_assign_zones(&map, UI_CTRL_SIDE_FRONT, &zone, 1, VITAKI_CTRL_OUT_L1);

  assert(map.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY] == OUT_NONE);
  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_FRONT, zone) == VITAKI_CTRL_OUT_L1);
  for (int i = 0; i < UI_CTRL_ZONES; i++) {
    if (i != zone)
      assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_FRONT, i) == OUT_TOUCHPAD);
  }
  assert(ui_controller_side_output(&map, UI_CTRL_SIDE_FRONT) == UI_CTRL_MIXED);
  assert(ui_controller_mapped_zones(&map, UI_CTRL_SIDE_FRONT) == UI_CTRL_ZONES);
}

/**
 * A zone listed in in_l2 but stored as None is L2; clearing it must really clear it, and in_l2 must
 * then stop naming it. Assigning L2 to a zone must be seen by the stream's trigger pointer.
 * catches: a cleared zone coming back as L2 after the next load, or in_l2 pointing at nothing
 * after L2 was assigned.
 */
static void test_trigger_pointers_follow_the_assignment(void) {
  ControllerMapStorage map = seeds();
  map.in_l2 = rear_cell(3);
  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_REAR, 3) == OUT_L2);

  const int zone = 3;
  ui_controller_assign_zones(&map, UI_CTRL_SIDE_REAR, &zone, 1, VITAKI_CTRL_OUT_NONE);
  assert(ui_controller_zone_output(&map, UI_CTRL_SIDE_REAR, 3) == OUT_NONE);
  assert(map.in_l2 != rear_cell(3));

  const int pair[] = {10, 11};
  ui_controller_assign_zones(&map, UI_CTRL_SIDE_REAR, pair, 2, VITAKI_CTRL_OUT_L2);
  assert(ui_controller_zones_output(&map, UI_CTRL_SIDE_REAR, pair, 2) == OUT_L2);
  assert(map.in_l2 != VITAKI_CTRL_IN_NONE);
  assert(map.in_out_btn[map.in_l2] == OUT_L2);
}

int main(void) {
  test_uniform_set_has_a_common_output();
  test_any_difference_is_mixed();
  test_empty_set_is_mixed();
  test_seeds_front_is_touchpad_rear_does_nothing();
  test_zone_disagreeing_with_whole_surface_is_mixed();
  test_assigning_the_whole_side_reads_back();
  test_assigning_one_zone_keeps_the_page_equal_to_the_stream();
  test_trigger_pointers_follow_the_assignment();
  printf("ui_controller_rules_tests: all passed\n");
  return 0;
}
