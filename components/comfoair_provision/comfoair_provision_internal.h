// The four helpers the two halves of this component share, and nothing else. comfoair_provision.c
// decides (init, the events, the clock) and comfoair_provision_query.c answers (every reader the
// API, the LED and the log go through); the file was 810 lines against a ceiling of 350, and this
// is the seam the file had already drawn for itself with a comment.
//
// NOT IN include/, and not one of these is part of the component's interface: they are the four
// rules that must be asked the SAME way on both sides -- what "elapsed" means across a 32-bit wrap,
// which of the window rule's two lengths a window has, whether it is still open, and which failures
// earn an access point. A second spelling of any of them is how a window closes in the projection
// and not in the machine, which is the class of defect this component has already paid for.
#pragma once

#include "comfoair_provision.h"

// "How much of `limit` is left since `since`", by unsigned subtraction and never by comparing
// against a stored deadline: a millisecond counter wraps every 49.7 days and a ventilation unit
// runs for years. `elapsed()` stays private to the deciding half, which is the only one that has a
// deadline to test.
uint32_t comfoair_prov_remaining(uint32_t now, uint32_t since, uint32_t limit);

// The window rule's two lengths, and 0 for the sealed-device rule's window that never closes.
uint32_t comfoair_prov_window_length(const comfoair_prov_t *s);

// The live question -- access point up, window running, neither clock run out -- as opposed to
// comfoair_prov_window_is_open(), which reports what the last tick decided.
bool comfoair_prov_window_open_at(const comfoair_prov_t *s, uint32_t now);

// The fallback names three of the four answers and excludes the fourth BY NAME. Two spellings of
// that line is how "no access point for a no-address failure" quietly becomes "usually".
bool comfoair_prov_failure_earns_the_access_point(comfoair_prov_failure_t failure);
