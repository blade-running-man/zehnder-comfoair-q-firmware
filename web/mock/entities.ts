// web/mock/entities.ts
// GET /api/state, GET /api/entities and GET /api/entities/<key>, in comfoair_api's shapes.
//
// The entity list IS the generated registry (src/api/entities.ts): the mock renders it, it does
// not keep one. A key added to pdo_table.py appears here on the next regenerate, as null until
// values.ts learns to drive it.
import { ENTITIES } from "../src/api/entities.ts";
import type { MockResponse, MockState } from "./model.ts";
import { stateValues } from "./values.ts";
import { HTTP, error } from "./wire.ts";

/** GET /api/state: a flat object, every registry key, `null` for "no reading yet". */
export function renderState(s: MockState): MockResponse {
  return { status: 200, body: stateValues(s) };
}

/**
 * GET /api/entities: a JSON ARRAY of metadata, comfoair_api_render_entities()'s key order.
 *
 * `stateClass` is rendered null: the firmware sends the registry's state_class, but the generated
 * TypeScript registry does not carry that column and the mock has no other source for it. Nothing
 * in the SPA reads it (EntityMeta has no such field), so null costs no screen anything.
 */
export function renderEntities(): MockResponse {
  return {
    status: 200,
    body: ENTITIES.map((e) => ({
      key: e.key, name: e.name, kind: e.kind, pdo: e.pdo, min: e.min, max: e.max,
      unit: e.unit, deviceClass: e.deviceClass, stateClass: null, icon: e.icon,
      entityCategory: e.entityCategory, writable: e.writable, options: e.options,
    })),
  };
}

/** GET /api/entities/<key>: {key, name, unit, value}; 404 "unknown entity" for a typo. */
export function renderEntity(s: MockState, key: string): MockResponse {
  const e = ENTITIES.find((x) => x.key === key);
  if (!e) return error(404, HTTP.unknownEntity);
  return { status: 200, body: { key: e.key, name: e.name, unit: e.unit, value: stateValues(s)[e.key] } };
}
