// What the page says when the device says no.
//
// Run: node src/pages/settings/tests/errors.test.ts
//
// The rule this file pins is that the DEVICE'S OWN words are what the owner reads. The page
// adds a heading that says what kind of answer it is; it never paraphrases the body, because
// the body is the only thing that carries which field was refused and why
// (comfoair_config_strerror() writes those sentences for exactly this).

import { ApiError } from "../../../api/client.ts";
import { explainError, oneLineFragment } from "../errors.ts";
import { eq, ok, report } from "./harness.ts";

// --- the device answered ----------------------------------------------------------------

{
  // 501 is the normal state of half this API right now: comfoair_http_assets.c's
  // comfoair_http_write_gate answers it for every write the access policy allows and no phase has
  // implemented (components/comfoair_http/comfoair_http_assets.c:16-29). Rendering that in red as a
  // fault teaches the owner to ignore red.
  const e = explainError(new ApiError(501, "not implemented yet"));
  eq(e.tone, "pending", "501 is not a fault: the route exists and this firmware has no code behind it");
  eq(e.status, 501, "the status is shown, because 501 and 500 mean opposite things here");
  eq(e.detail, "not implemented yet", "and the device's own sentence is passed through");
}

{
  const e = explainError(new ApiError(401, "password required"));
  eq(e.tone, "denied", "401 is the web-interface password, not a broken request");
  eq(e.detail, "password required", "verbatim");
}

{
  // On its own access point an unclaimed device refuses every write but /api/provision
  // (components/comfoair_policy/comfoair_http_policy.c:31-46). A 403 on the broker form is
  // therefore expected behaviour with a cause the owner can act on, not a bug.
  const e = explainError(new ApiError(403, "not allowed while unprovisioned"));
  eq(e.tone, "denied", "403 is the access policy speaking");
  eq(e.detail, "not allowed while unprovisioned", "verbatim");
}

{
  // 404 is what this device answers TODAY for GET /api/config: no handler is registered and
  // asset_get refuses to hand the SPA shell back for an /api/ path
  // (components/comfoair_http/comfoair_http_assets.c:48-56). The UI and the firmware ship as ONE
  // artefact -- vite's output is compiled in with EMBED_FILES (web/README.md) -- so a route
  // this page calls and the device does not have cannot be a version mismatch. It can only be
  // a handler this build has not grown yet, which is the same news as 501 and not a fault.
  const e = explainError(new ApiError(404, "no such endpoint"));
  eq(e.tone, "pending", "404 on a route this UI calls means this build has no handler for it");
  eq(e.status, 404, "and the status says which of the two kinds of 'not yet' it is");
  ok(e.headline !== explainError(new ApiError(501, "x")).headline,
     "with wording of its own: 501 is a declared route, 404 is no route at all");
}

{
  eq(explainError(new ApiError(400, "mqtt port out of range")).tone, "error", "4xx is a refusal");
  eq(explainError(new ApiError(500, "nvs write failed")).tone, "error", "5xx is a fault");
}

{
  // The whole contract in one loop: whatever the status, the body is not rewritten.
  for (const status of [400, 401, 403, 404, 409, 500, 501, 503]) {
    const said = `device sentence ${status}`;
    eq(explainError(new ApiError(status, said)).detail, said,
       `the ${status} body reaches the owner unchanged`);
  }
}

// --- the device did not answer ------------------------------------------------------------

{
  // fetch() rejects with a TypeError and no status. It carries no information about the
  // device at all, so the page must not put words in its mouth.
  const e = explainError(new TypeError("Failed to fetch"));
  eq(e.tone, "offline", "no answer is its own kind of answer");
  eq(e.status, null, "and there is no status to show");
}

{
  // After POST /api/provision, losing the page is the SUCCESSFUL outcome: the device answers
  // first and only then retunes the radio (docs/architecture/failure-model.md), and the
  // access point it is serving this page from goes with it.
  const e = explainError(new TypeError("Failed to fetch"), { disconnectExpected: true });
  eq(e.tone, "pending", "a lost connection right after provisioning is not a failure");
  ok(e.headline !== explainError(new TypeError("x")).headline,
     "and it does not read like one either");
}

{
  const e = explainError("something threw a string");
  ok(e.detail.length > 0 && !e.detail.includes("undefined") && !e.detail.includes("object Object"),
     "a thrown non-Error still produces a sentence rather than [object Object]");
}

// --- oneLineFragment: Explanation collapsed to one line, for State.tsx's write() -------------

{
  // THE CASE THIS FUNCTION EXISTS FOR. The real firmware answers 401 with the literal body
  // {"error":"unauthorized"} (comfoair_http_gate.c:135-140) -- one bare word, not a sentence -- and
  // the actionable half ("wants its web-interface password") lives only in the headline. Detail
  // alone would render "Not sent: unauthorized" on the State page, which tells the owner nothing
  // they can act on.
  const e = explainError(new ApiError(401, "unauthorized"));
  eq(
    oneLineFragment(e),
    "The device wants its web-interface password (unauthorized)",
    "401 composes the actionable headline with the device's own word",
  );
}

{
  // The real firmware's 403 body is equally bare: {"error":"forbidden"}
  // (comfoair_http_gate.c:141-146).
  const e = explainError(new ApiError(403, "forbidden"));
  eq(
    oneLineFragment(e),
    "The device refused this write (forbidden)",
    "403 composes too -- both denied-tone statuses answer with a single word",
  );
}

{
  // Every other tone's `.detail` is already a full sentence (comfoair_config_strerror() writes
  // those), so composing the headline in too would only pad an already-complete line.
  const e = explainError(new ApiError(422, "unknown option"));
  eq(oneLineFragment(e), "unknown option", "a non-denied tone is passed through unchanged");
}

{
  const e = explainError(new TypeError("Failed to fetch"));
  eq(
    oneLineFragment(e),
    "Nothing reached the device, so it has said nothing about this. (Failed to fetch)",
    "an offline explanation is also passed through unchanged -- it is already one full sentence",
  );
}

report("errors");
