# Web UI

Preact + Vite + TypeScript single-page application for the ComfoAir Q firmware.

It is not uploaded to the device. Vite gzips the bundle to three files and the
firmware build compiles them in with `EMBED_FILES`, so the UI and the firmware
are one artefact and an OTA can never leave a new binary serving an old UI. A file
system would also cost a partition of flash to hold a few tens of kilobytes, and a
second update procedure beside the `.bin`. See `../docs/interfaces/web-ui.md`.

```sh
npm run dev     # dev server; proxies /api and /ws to the device
npm run build   # three .gz files into dist/ -- the firmware build runs this itself
```

Point the dev server at a device with `VITE_DEVICE_IP` in `.env.local`.

## Rules

- **No privileged endpoints.** Everything this UI calls must be reachable by any
  other client through the same API (AGENTS.md criterion 4). A path that exists only for
  a screen here is a bug in the API, not a shortcut.
- **`api/entities.ts` is generated**, not hand-edited: the single source of truth for
  entities is `tools/pdo_table.py` (see `../docs/development/entity-registry.md`). The device CONFIG is
  not in that table, and its hand-written shapes live beside the code that speaks
  it (`DeviceConfig`/`ConfigPatch` in `api/client.ts`); there is no `api/types.ts`.
- **Secrets are never rendered into an input.** See `SecretField.tsx` -- the device
  returns a sentinel in place of a stored password and the field must stay empty.
