# Conventions

The rules below are the ones that carry load: each exists because breaking it breaks something a
user would notice, usually quietly. They apply to every change, human or agent. They are written
down in `AGENTS.md`; this page says, for each one, what (if anything) actually checks it.

## Size: 350 lines per file, 600 per suite

A hand-written source file stays under **350 lines**. A Unity test suite may reach **600**,
because a suite is one binary per directory and therefore splits by directory, not by file.
Generated files (`registry_generated.h`, `entities.ts`, `entityNames.generated.ts`) are exempt.

When a file needs splitting, cut along a seam that already exists: one responsibility per file,
the public header unchanged. A component is a directory, so `comfoair_ha.c`,
`comfoair_ha_payload.c` and `comfoair_ha_cleanup.c` side by side, sharing
`comfoair_ha_internal.h`, is the shape to reach for — not one thousand-line file. Many files are
over the ceiling today: 14 hand-written sources outside the test suites, counted over
`components/`, `src/`, `web/src/` and `tools/` (the largest are `comfoair_json.cpp`,
`comfoair_config_nvs.c` and `comfoair_provision.c`, each between six and seven hundred lines;
`tools/pdo_table.py` is over by the owner's standing exemption), and ten Unity
suites, counting every `.c` and `.cpp` in a suite's directory. Split them when you next work
inside them, not in a drive-by rewrite. Count with `wc -l` before acting: line counts drift,
and a remembered number sends you to split a file that is already split.

**What enforces it:** nothing in the build or the tests. The only mechanism is a Claude Code
`PostToolUse` hook, `.claude/hooks/file-size-guard.sh` (wired in `.claude/settings.json`), which
runs after an agent's Write or Edit and *warns* — it never blocks. It counts per file, applying
600 to any path under `test/` or `tests/` (or named `test_*`) and 350 to other `.c`, `.h`, `.cpp`,
`.hpp`, `.ts`, `.tsx` and `.py` files, and skips `registry_generated.h`, `sdkconfig*`,
`package-lock.json`, `node_modules/`, `.pio/` and `dist/`.

## Comments say why

Every non-trivial function carries a comment that says **why** it is the way it is, citing the
source line, reference section or measurement that proves it. Where the obvious improvement would
break something, the comment says `DO NOT` and explains what breaks:

```c
// DO NOT widen this to plain -Werror: the fakes under test/*/idf and Unity's
// macros are exactly why the rest of -Wextra is advisory here.
```

Comments that restate the code are noise — delete them.

## Decisions are recorded where they live

A decision a reader could reasonably second-guess is recorded where it lives, in English and
self-contained: in a comment beside the code, or in the owning docs page under "Why not …". State
the rejected alternative and the evidence (`file:line` or a measured result), and what test or
hardware scenario pins it. When a decision is reversed, rewrite the record to the current truth
and keep one line on why the earlier option failed. There is no separate history file.

**What enforces it:** review. Nothing automated checks it.

## Public headers are contracts

Every public header under `components/<name>/include/` documents its contract:

- **ownership** — who allocates, who frees, how long a returned pointer lives;
- **task context** — which task or context may call it, and whether it blocks;
- **failure** — what happens on failure, and which `esp_err_t` values a caller must handle.

A caller should be able to use a component from its header alone.

## No Arduino

No Arduino core, no Arduino library, not even transitively through a third-party component.
`platformio.ini` uses `framework = espidf` and must never gain `framework = arduino`, and no
source includes `Arduino.h`. The absence is acceptance criterion 7 in `AGENTS.md`, which describes
it as a `grep` over the tree; no test, hook or CI step runs that search. Today the tree has no
`framework = arduino` and no `Arduino.h` include — the word appears only in comments.

## No GPIO number outside `components/board/`

Pins are named in exactly one place: the board descriptor in `components/board/`, read through
`board_get()`. Everything else takes its pins from there. This is what keeps the board boundary
real — adding a board means adding a descriptor, and touches nothing else. A raw pin number in
any other component is a bug even when it is the right number. No automated check enforces this;
`test_can_transport` pins that the CAN controller takes its pins from the board descriptor, and a
search for GPIO numbers outside `components/board/` currently finds none.

## Secrets are write-only

A stored password is never returned by any projection of the API. A read returns the sentinel
`__UNCHANGED__` when a secret is set and an empty string when none is; sending the sentinel back
keeps the stored value, and the sentinel is refused as a new password when nothing is stored
(otherwise a published string would become the password of every unconfigured device). Which
keys count as secrets is decided by substring, case-insensitively: any key containing `password`,
`passwd`, `psk`, `secret`, `token` or `key` (`comfoair_secret_key()` in
`components/comfoair_secrets/comfoair_secrets.c`), so a future field is redacted without anyone
having to remember a list of fields. `test_secrets` pins these rules. This is what makes a single
published binary safe to configure in the browser.

## Nothing reboots because a peer is absent

No "broker unreachable for N minutes, restart". A missing broker, network or Home Assistant is a
state the device reports and recovers from by retrying — never by restarting, and a reboot on an
absent broker must never be introduced. The tree has exactly two `esp_restart()` calls: OTA's restart into a new image
(`components/comfoair_ota_runtime/comfoair_ota_runtime.c`) and the button's reset
(`components/comfoair_button/comfoair_button_reset.c`).

## The web UI has no privileged endpoint

Everything the web UI does goes through the same public API as any other client. If a screen
would be easier with a path of its own, the API is wrong, and the fix belongs in the API. Every
command — an entity write or one of the six operations — is built by the same
`comfoair_command` functions whether it arrives over REST (`/api/entities/{key}`,
`/api/ops/{name}`) or over MQTT.

## One entity list

Entities are declared once, in `tools/pdo_table.py`, and everything else is generated. Never edit
a generated file, and never write a second list — not a TypeScript union of keys, not a table in
a document. See [Entity registry](/development/entity-registry).

## The platform pin is load-bearing

`platformio.ini` pins `pioarduino/platform-espressif32#55.03.311`. A floating platform URL once
moved the toolchain under a design that had been verified against a different ESP-IDF tree. Bump
the pin only deliberately, re-check the ESP-IDF behaviour the design relies on, and say so in the
change. See [Building](/development/building).

## Tests first, on the host

The protocol, state, policy, JSON and command layers are tested on the host, and the test is
written before the implementation. Expected values come from reference documents and captured
device data, not from the code under test. Fakes belong to one suite and are never shared. See
[Testing](/development/testing).

## Committing

Nothing is committed or pushed without the owner's permission. Verification is output, not
assertion: a claim that tests pass comes with the output that shows it.
