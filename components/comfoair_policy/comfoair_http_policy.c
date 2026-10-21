#include "comfoair_http_policy.h"

#include <string.h>

// Exact match, never a prefix. "/api/configuration" must not inherit the bootstrap
// exemption that "/api/config" has, and getting this wrong is the usual way a rule like
// this leaks -- so it is a function with a name rather than a strncmp at each call site.
static bool path_is(const char *path, const char *expected)
{
    return path != NULL && strcmp(path, expected) == 0;
}

// A path UNDER a prefix, with something after it. The two command routes address one entity or one
// operation each, so "under" is what has to be asked -- but a prefix rule is how an exemption
// leaks, and this is written once with three properties rather than as a strncmp at each call
// site:
//
//   * the prefix must END IN A SLASH, so "/api/entities-secret" cannot inherit "/api/entities"'s
//     rule by sharing its first fourteen letters;
//   * something must FOLLOW it, so the bare stem and a trailing slash are not covered -- there is
//     no entity called "" and a rule that covered one would be a rule about a route, not a
//     resource;
//   * nothing is normalised. "/api/entities/../ota" matches, and that is safe rather than
//     overlooked: esp_http_server dispatched it to the entity handler on the same string, so the
//     only thing it can reach is an entity lookup that will not find it.
static bool path_under(const char *path, const char *prefix)
{
    if (path == NULL || prefix == NULL)
        return false;
    const size_t n = strlen(prefix);
    if (n == 0 || prefix[n - 1] != '/')
        return false;
    return strncmp(path, prefix, n) == 0 && path[n] != '\0';
}

// Changing the ventilation, as distinct from changing the device. The difference is what makes the
// exemption below defensible: a fan speed is what anybody who can walk up to the unit can already
// change, and an OTA image is not.
static bool path_is_a_command(const char *path)
{
    return path_under(path, "/api/entities/") || path_under(path, "/api/ops/");
}

// Starting the RMI probe: a READ-ONLY walk of the unit's settings, rendered later by GET
// /api/probe. It shares the command routes' no-password exemption because reading the unit's
// configuration is no more than changing its fan speed, which that exemption already allows --
// whoever may change the ventilation may also see how the unit is set up. With a password set it
// needs authentication like every write, and on an unclaimed access point it is refused like every
// command (both fall out of the order in comfoair_http_check, not out of this function).
//
// EXACT path, and DO NOT fold it into path_is_a_command() as path_under("/api/probe/"): the walk
// has one route and no sub-resources, and a prefix here would hand the exemption to any route a
// later change hangs under /api/probe/ -- an erase or a write is not a read.
static bool path_is_the_probe(const char *path)
{
    return path_is(path, "/api/probe");
}

comfoair_http_decision_t comfoair_http_check(comfoair_http_method_t method, const char *path,
                                             comfoair_http_ctx_t ctx)
{
    if (path == NULL)
        return COMFOAIR_HTTP_FORBIDDEN;

    if (method == COMFOAIR_HTTP_GET) {
        // Reading is open until the owner draws a boundary. Ventilation temperatures are
        // not worth locking someone out of their own device over, and on the access point
        // the setup page has to be reachable by definition.
        if (!ctx.password_set)
            return COMFOAIR_HTTP_ALLOW;
        return ctx.authenticated ? COMFOAIR_HTTP_ALLOW : COMFOAIR_HTTP_UNAUTHORIZED;
    }

    if (method != COMFOAIR_HTTP_POST)
        return COMFOAIR_HTTP_FORBIDDEN;

    if (!ctx.provisioned) {
        // Radio proximity is not authorisation. The one thing an unprovisioned device must
        // accept is a network to join, or it is a brick; the setup window puts a clock on
        // that, because an access point that takes a network from anyone for as long as it is
        // up hands control to whoever is in radio range.
        if (!path_is(path, "/api/provision"))
            return COMFOAIR_HTTP_FORBIDDEN;

        // But "nobody owns this device yet" and "its router died" are different situations
        // wearing the same clothes. Both show an access point and no connection; only the
        // second one holds somebody's data. Once a password exists the device has an owner,
        // and re-homing it to another network is theirs to authorise -- otherwise a
        // neighbour waits for a power cut and takes the device out of the house.
        if (ctx.password_set)
            return ctx.authenticated ? COMFOAIR_HTTP_ALLOW : COMFOAIR_HTTP_UNAUTHORIZED;
        return COMFOAIR_HTTP_ALLOW;
    }

    if (!ctx.password_set) {
        // Setting the first password is one of two writes that get through, or the device could
        // never be secured: every write needs a password and the password is a write.
        //
        // The other is CONTROLLING THE VENTILATION, and it is here because the password is optional
        // and off by default, and a device without one (sealed behind the unit's panel, out of
        // reach of the button) is a supported configuration. With only /api/config exempt, those
        // two together meant every command answered 403 on a device out of the box, until
        // the owner set a password nothing told them to set -- which is a feature that does not
        // work rather than a device that is secure. Found by counter-review of phase 7; the owner
        // chose this trade on 2026-09-04.
        //
        // WHAT IT COSTS, said plainly: anybody on the household LAN can change the ventilation.
        // That is the same thing anybody who can walk up to the unit's own screen can already do.
        //
        // Everything else still fails CLOSED -- an OTA endpoint answering before anyone has
        // claimed the device is unauthenticated code execution on the LAN, and this firmware does
        // not verify image signatures. That is a different question from a fan speed.
        if (path_is(path, "/api/config") || path_is_a_command(path) || path_is_the_probe(path))
            return COMFOAIR_HTTP_ALLOW;
        return COMFOAIR_HTTP_FORBIDDEN;
    }

    return ctx.authenticated ? COMFOAIR_HTTP_ALLOW : COMFOAIR_HTTP_UNAUTHORIZED;
}
