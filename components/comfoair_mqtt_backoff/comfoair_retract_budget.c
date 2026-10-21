// One line, and the header carries the whole argument for why it is a file rather than a static
// helper next to its only caller: comfoair_mqtt does not build on the host, so anything that lives
// there is unreachable by every suite in this tree.
#include "comfoair_retract_budget.h"

bool comfoair_retract_continue(size_t index, size_t total, uint32_t elapsed_ticks,
                               uint32_t budget_ticks)
{
    // BOTH CLAUSES, and neither is decoration. Dropping the first turns the sweep into two seconds
    // of asking comfoair_ha_cleanup_topic() for topics past the end; dropping the second is the
    // missing ceiling itself. `<` and not `<=` on the budget: spent is spent on the tick it is
    // reached.
    return index < total && elapsed_ticks < budget_ticks;
}
