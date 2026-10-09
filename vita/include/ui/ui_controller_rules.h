/**
 * @file ui_controller_rules.h
 * @brief Pure rules of the Controller page (SPEC.md section 3.8)
 *
 * No Vita, context or controller dependency, so the rules can be checked natively
 * (test/ui_controller_rules_tests.c).
 */

#pragma once

/** ui_controller_common_output() result when the outputs of a set of inputs differ. */
#define UI_CTRL_MIXED (-1)

/**
 * ui_controller_common_output() - The one output every input of a set is mapped to.
 * @outputs: The output of each input in the set.
 * @count:   How many; the set must not be empty.
 *
 * The mapping popup ticks this output; when the inputs differ it reads "Mixed" and ticks nothing.
 * None (0) is an output like any other: a set that is all None has the common output None.
 *
 * @return the common output, or UI_CTRL_MIXED when the outputs differ or the set is empty
 */
int ui_controller_common_output(const int *outputs, int count);
