// The marker the device sends in place of a stored secret.
//
// Must equal COMFOAIR_SECRET_SENTINEL in components/comfoair_secrets/include/comfoair_secrets.h.
// The round trip is: GET returns this for a secret that is set -> the page renders it in a
// password field and submits the whole document back -> POST treats it as "unchanged" and
// keeps the stored value. A sentinel rather than an omitted key, because the page echoes
// back everything it loaded: with the key omitted, an untouched password would arrive as ""
// and be indistinguishable from a deliberate clear, so one save of an unrelated setting
// would wipe the broker credentials.
export const CONFIG_UNCHANGED = "__UNCHANGED__";
