// The GET /api/system document: one bounded buffer, every null rule here
// so that the HTTP handler only gathers numbers and never decides what "unknown" looks like.
//
// Compact JSON, keys in the schema's order. The document is a snapshot read once by a page, not
// a stream, so it is rendered whole into the caller's buffer -- unlike the probe report, whose
// size depends on the walk and which is therefore streamed.
#include <cstdio>
#include <cstring>

#include "comfoair_probe_report.h"
#include "comfoair_sysinfo.h"

namespace {

// Appends into [out, out + cap). Once anything does not fit it stays failed and writes nothing
// more, so the caller checks once, at the end, and a short buffer can never be overrun.
class Out {
public:
    Out(char *out, size_t cap) : out_(out), cap_(cap) {}

    void put(const char *s, size_t n)
    {
        // `< cap_`, not `<=`: one byte is always kept for the NUL.
        if (!ok_ || n >= cap_ - len_) {
            ok_ = false;
            return;
        }
        std::memcpy(out_ + len_, s, n);
        len_ += n;
    }
    void put(const char *s) { put(s, std::strlen(s)); }

    void put_uint(unsigned long long v)
    {
        char tmp[24];
        const int n = std::snprintf(tmp, sizeof tmp, "%llu", v);
        put(tmp, (size_t)n);
    }
    void put_int(long long v)
    {
        char tmp[24];
        const int n = std::snprintf(tmp, sizeof tmp, "%lld", v);
        put(tmp, (size_t)n);
    }
    void put_bool(bool b) { put(b ? "true" : "false"); }

    // A JSON string. `own` text is this firmware's UTF-8 (the error table: "…", "CO₂") and passes
    // bytes >= 0x80 through. Everything else is escaped byte-wise: unit strings come off the bus
    // and an SSID is whatever the access point broadcasts, so neither is known to be UTF-8, and
    // a byte < 0x20 or >= 0x7F as \u00XX (read as Latin-1) is always valid JSON. DO NOT pass
    // non-`own` bytes through: one stray 0xFF in a serial makes the whole document unparseable,
    // and the System page is the one an owner opens when something is already wrong.
    void put_string(const char *s, size_t n, bool own = false)
    {
        static const char HEX[] = "0123456789abcdef";
        put("\"");
        for (size_t i = 0; i < n && ok_; i++) {
            const uint8_t c = (uint8_t)s[i];
            if (c == '"' || c == '\\') {
                const char esc[2] = {'\\', (char)c};
                put(esc, 2);
            } else if (c < 0x20 || (!own && c >= 0x7F)) {
                const char esc[6] = {'\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 15]};
                put(esc, 6);
            } else {
                put((const char *)&c, 1);
            }
        }
        put("\"");
    }

    // NULL and "" are both null: the schema has no empty-string state, and the snapshot spells
    // "not known" either way (comfoair_sysinfo.h: ssid "NULL or \"\" when not connected").
    void put_text(const char *s)
    {
        if (s == nullptr || s[0] == '\0')
            put("null");
        else
            put_string(s, std::strlen(s));
    }

    // A unit string field: bounded by its array, because it is filled from bus bytes and a full
    // field may carry no NUL. DO NOT strlen(): it would run on into the next field.
    void put_field(const char (&f)[COMFOAIR_SYSINFO_TEXT_MAX])
    {
        const void *nul = std::memchr(f, 0, sizeof f);
        const size_t n = nul ? (size_t)((const char *)nul - f) : sizeof f;
        if (n == 0)
            put("null");
        else
            put_string(f, n);
    }


    size_t finish()
    {
        if (!ok_) {
            if (cap_ > 0)
                out_[0] = '\0';
            return 0;
        }
        out_[len_] = '\0';
        return len_;
    }

private:
    char *out_;
    size_t cap_;
    size_t len_ = 0;
    bool ok_ = true;
};

const char *read_name(comfoair_sysinfo_read_t r)
{
    switch (r) {
    case COMFOAIR_SYSINFO_READ_OK: return "ok";
    case COMFOAIR_SYSINFO_READ_NO_ANSWER: return "no_answer";
    case COMFOAIR_SYSINFO_READ_UNKNOWN: break;
    }
    return "unknown";
}

void render_errors(Out &o, const comfoair_sysinfo_unit_t &u)
{
    o.put("{\"state\":\"");
    o.put(read_name(u.errors_state));
    o.put("\",\"ids\":[");
    // The bits are shown only from an answered read: after a failed one the array may hold an
    // older answer, and an error that cleared meanwhile must not stay on screen as active.
    const bool ok = u.errors_state == COMFOAIR_SYSINFO_READ_OK;
    // Each byte walked from its least significant bit: byte i, bit b is id i*8 + b.
    bool first = true;
    for (unsigned id = 0; ok && id < COMFOAIR_SYSINFO_ERRORS_BYTES * 8; id++) {
        if (!(u.errors[id / 8] & (1u << (id % 8))))
            continue;
        if (!first)
            o.put(",");
        first = false;
        o.put_uint(id);
    }
    o.put("],\"items\":[");
    first = true;
    for (unsigned id = 0; ok && id < COMFOAIR_SYSINFO_ERRORS_BYTES * 8; id++) {
        if (!(u.errors[id / 8] & (1u << (id % 8))))
            continue;
        o.put(first ? "{\"id\":" : ",{\"id\":");
        first = false;
        o.put_uint(id);
        o.put(",\"text\":");
        const char *t = comfoair_sysinfo_error_text((uint16_t)id, u.has_firmware, u.firmware_raw);
        if (t == nullptr)
            o.put("null");
        else
            o.put_string(t, std::strlen(t), true);
        o.put("}");
    }
    o.put("]}");
}

void render_unit(Out &o, const comfoair_sysinfo_unit_t &u)
{
    o.put("{\"state\":\"");
    o.put(read_name(u.identity_state));
    o.put("\",\"age_ms\":");
    if (u.identity_ever_read)
        o.put_uint(u.identity_age_ms);
    else
        o.put("null");
    o.put(",\"model\":");
    o.put_field(u.model);
    o.put(",\"serial\":");
    o.put_field(u.serial);
    o.put(",\"firmware\":");
    char fw[16];  // version_text's worst case, "R1023.1023.1023" + NUL
    const size_t fw_len =
        u.has_firmware ? comfoair_probe_version_text(u.firmware_raw, fw, sizeof fw) : 0;
    if (fw_len == 0)
        o.put("null");
    else
        o.put_string(fw, fw_len);
    o.put(",\"firmware_raw\":");
    if (u.has_firmware)
        o.put_uint(u.firmware_raw);
    else
        o.put("null");
    o.put(",\"article\":");
    o.put_field(u.article);
    o.put(",\"country\":");
    o.put_field(u.country);
    o.put(",\"name\":");
    o.put_field(u.name);
    o.put(",\"errors\":");
    render_errors(o, u);
    o.put("}");
}

void render_device(Out &o, const comfoair_sysinfo_device_t &d)
{
    o.put("{");
    o.put("\"firmware\":");
    o.put_text(d.firmware);
    o.put(",\"idf\":");
    o.put_text(d.idf);
    o.put(",\"built\":");
    o.put_text(d.built);
    o.put(",\"board\":");
    o.put_text(d.board);
    o.put(",\"chip\":");
    o.put_text(d.chip);
    o.put(",\"chip_revision\":");
    o.put_uint(d.chip_revision);
    o.put(",\"flash_bytes\":");
    o.put_uint(d.flash_bytes);
    o.put(",\"partition\":");
    o.put_text(d.partition);
    o.put(",\"uptime_ms\":");
    o.put_uint(d.uptime_ms);
    o.put(",\"reset_reason\":");
    o.put_text(d.reset_reason);
    o.put(",\"heap_free\":");
    o.put_uint(d.heap_free);
    o.put(",\"heap_min_free\":");
    o.put_uint(d.heap_min_free);
    o.put(",\"heap_largest_block\":");
    o.put_uint(d.heap_largest_block);
    o.put(",\"device_id\":");
    o.put_text(d.device_id);
    o.put(",\"mac\":");
    o.put_text(d.mac);
    o.put("}");
}

void render_network(Out &o, const comfoair_sysinfo_network_t &n)
{
    // Disconnected: every link field is null whatever else the snapshot holds. An SSID or IP
    // beside "connected": false reads as "connected to", and the schema's rule is null without
    // a network, not "the last one".
    o.put("{\"connected\":");
    o.put_bool(n.connected);
    o.put(",\"ssid\":");
    o.put_text(n.connected ? n.ssid : nullptr);
    o.put(",\"ip\":");
    o.put_text(n.connected ? n.ip : nullptr);
    o.put(",\"rssi\":");
    if (n.connected && n.has_rssi)
        o.put_int(n.rssi);
    else
        o.put("null");
    o.put(",\"channel\":");
    if (n.connected && n.channel != 0)
        o.put_uint(n.channel);
    else
        o.put("null");
    o.put("}");
}

void render_bus(Out &o, const comfoair_sysinfo_bus_t &b)
{
    o.put("{");
    o.put("\"started\":");
    o.put_bool(b.started);
    o.put(",\"state\":");
    o.put_text(b.state);
    o.put(",\"rx_frames\":");
    o.put_uint(b.rx_frames);
    o.put(",\"rx_dropped\":");
    o.put_uint(b.rx_dropped);
    o.put(",\"bus_errors\":");
    o.put_uint(b.bus_errors);
    o.put(",\"bus_recoveries\":");
    o.put_uint(b.bus_recoveries);
    o.put(",\"tx_error_count\":");
    o.put_uint(b.tx_error_count);
    o.put(",\"rx_error_count\":");
    o.put_uint(b.rx_error_count);
    o.put(",\"nodes_seen\":[");
    // Cut at the header's bound: the handler's buffer is sized for COMFOAIR_SYSINFO_MAX_NODES,
    // and a longer list would turn into a 0 return -- no document -- instead of a shorter one.
    size_t count = b.nodes_seen != nullptr ? b.nodes_seen_count : 0;
    if (count > COMFOAIR_SYSINFO_MAX_NODES)
        count = COMFOAIR_SYSINFO_MAX_NODES;
    for (size_t i = 0; i < count; i++) {
        if (i > 0)
            o.put(",");
        o.put_uint(b.nodes_seen[i]);
    }
    o.put("]}");
}

}  // namespace

size_t comfoair_sysinfo_render(const comfoair_sysinfo_t *info, char *out, size_t cap)
{
    Out o(out, cap);
    o.put("{\"unit\":");
    render_unit(o, info->unit);
    o.put(",\"device\":");
    render_device(o, info->device);
    o.put(",\"network\":");
    render_network(o, info->network);
    o.put(",\"bus\":");
    render_bus(o, info->bus);

    const comfoair_sysinfo_commands_t &c = info->commands;
    o.put(",\"commands\":{");
    o.put("\"queued\":");
    o.put_uint(c.queued);
    o.put(",\"sent\":");
    o.put_uint(c.sent);
    o.put(",\"failed\":");
    o.put_uint(c.failed);
    o.put(",\"dropped\":");
    o.put_uint(c.dropped);

    const comfoair_sysinfo_mqtt_t &m = info->mqtt;
    o.put("},\"mqtt\":{");
    o.put("\"configured\":");
    o.put_bool(m.configured);
    o.put(",\"connected\":");
    o.put_bool(m.connected);
    o.put(",\"published\":");
    o.put_uint(m.published);
    o.put(",\"commands\":");
    o.put_uint(m.commands);
    o.put(",\"rejected\":");
    o.put_uint(m.rejected);
    o.put(",\"reconnects\":");
    o.put_uint(m.reconnects);
    o.put("}}");
    return o.finish();
}
