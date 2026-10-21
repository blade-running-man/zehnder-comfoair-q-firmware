// One condition and one format string, and the header carries the whole argument for why they are
// a file here rather than four lines inside comfoair_mqtt_client.c beside the buffer they write:
// that component is not built by [env:native], so a condition living in it is checked by nothing
// at all. The third lodger to arrive on that finding, after comfoair_retract_budget.c and
// comfoair_discovery_gate.c.
#include "comfoair_device_url.h"

#include <stdio.h>
#include <string.h>

bool comfoair_device_url_refresh(char *url, size_t size, const char *ip)
{
    if (url == NULL || size == 0)
        return false;
    // NO ADDRESS IS NOT A CHANGE. The header says why the old value is kept rather than blanked;
    // the short version is that a Wi-Fi blip would otherwise cost two discovery sweeps and take
    // the owner's link away in the middle of the outage.
    if (ip == NULL || ip[0] == '\0')
        return false;

    // BUILT IN FULL BEFORE ANYTHING IS COMPARED OR WRITTEN, on the caller's stack rather than into
    // `url`. Formatting straight into the destination would destroy the value being compared
    // against, so a url that turned out not to fit -- or not to have moved -- could not be put
    // back.
    char built[COMFOAIR_DEVICE_URL_MAX];
    const int n = snprintf(built, sizeof built, "http://%s/", ip);
    // A TRUNCATION IS A REFUSAL AND NOT A SHORTER URL. snprintf answers the length it WOULD have
    // written, so this catches an `ip` that is not a dotted quad as well as a caller whose buffer
    // is smaller than this one. Absent beats wrong: Home Assistant renders "http://192.168.1"
    // perfectly and sends the owner nowhere.
    if (n < 0 || (size_t)n >= sizeof built || (size_t)n >= size)
        return false;

    // THE CONDITION IS "MOVED", AND THE WHOLE FIX TURNS ON IT BEING THIS AND NOT "IS EMPTY".
    // Empty answers the boot case -- the client was built before the radio associated -- and gets
    // the router reboot wrong, freezing the card on the first address the device ever had. This
    // one answers both, and answers false on every pass that changed nothing, which is what stops
    // the whole retained discovery tree going round in a circle on a task that wakes several
    // times a second.
    if (strcmp(url, built) == 0)
        return false;

    memcpy(url, built, (size_t)n + 1);
    return true;
}
