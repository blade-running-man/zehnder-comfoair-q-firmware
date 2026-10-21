// Turning a failed request into something an owner can act on.
//
// One rule, and tests/errors.test.ts pins it for every status: THE DEVICE'S OWN SENTENCE IS
// WHAT IS SHOWN. comfoair_config_strerror() writes those sentences for exactly this purpose,
// and it is the only thing that knows which field was refused. This file adds a heading that
// says what KIND of answer arrived; it never rewrites the body -- it only passes the device's
// sentence through translateDetail() (i18n/detail.ts): a firmware sentence this SPA has a
// template for comes back in the owner's language, and one it does not is shown in English
// rather than paraphrased. In English the template's value IS the sentence, byte for byte.
//
// The Explanation is built in the language current when the request failed. A switch after
// that leaves an already-shown notice in the old language until the next failure replaces it;
// that is accepted rather than carrying keys through state, because a notice is about one
// moment and is short-lived.
//
// Relative imports below carry the .ts extension, unlike the .tsx files in this page.
// That is what lets node resolve this module when the suite runs it directly
// (tests/harness.ts explains why the suite is run that way); Vite is indifferent, and
// `allowImportingTsExtensions` is already on in tsconfig.app.json.

import { ApiError } from "../../api/client.ts";
import { translateDetail } from "../../i18n/detail.ts";
import { t } from "../../i18n/index.ts";

/**
 * How loudly to say it.
 *
 * "pending" exists because of 501. Half of this API answers 501 today -- comfoair_http_write_gate
 * returns it for every write the access policy allows and no phase has implemented
 * (components/comfoair_http/comfoair_http_assets.c:16-29) -- and that is a true statement about the
 * firmware's progress, not a fault in the device or in what the owner typed. Painting it red
 * teaches the owner that red means nothing.
 */
export type ErrorTone = "pending" | "denied" | "error" | "offline";

export interface Explanation {
  tone: ErrorTone;
  /** The HTTP status, or null when nothing answered. */
  status: number | null;
  /** This page's one sentence about what kind of answer this is. */
  headline: string;
  /** What the device said, unedited -- or, when nothing answered, who is speaking instead. */
  detail: string;
}

export function explainError(
  err: unknown,
  opts: { disconnectExpected?: boolean } = {},
): Explanation {
  if (err instanceof ApiError) {
    // 501 is tested before the 5xx bucket it belongs to, deliberately: 500 and 501 differ by
    // one digit and mean opposite things here -- "this device is broken" against "this
    // firmware has not got there yet".
    if (err.status === 501)
      return {
        tone: "pending",
        status: err.status,
        headline: t("settings.error.notImplemented.headline"),
        detail: translateDetail(err.message),
      };

    // 404 is what this device answers today for a phase-5 route: no handler is registered, and
    // asset_get refuses to return the SPA shell for an /api/ path so that "missing endpoint"
    // cannot masquerade as "the endpoint returned HTML" (comfoair_http_assets.c:48-56). The UI is
    // compiled into the same binary as the firmware (web/README.md), so this page can never be
    // a version ahead of the device by accident -- a 404 here means this build has no handler,
    // which is news about the firmware's progress and not about the request.
    if (err.status === 404)
      return {
        tone: "pending",
        status: err.status,
        headline: t("settings.error.noHandler.headline"),
        detail: translateDetail(err.message),
      };

    if (err.status === 401)
      return {
        tone: "denied",
        status: err.status,
        headline: t("settings.error.passwordNeeded.headline"),
        detail: translateDetail(err.message),
      };

    // 403 is the access policy, and on an unclaimed device it is the EXPECTED answer to
    // everything except joining a network (comfoair_policy/comfoair_http_policy.c:31-46). It
    // is a refusal with a cause the owner can act on, not a malfunction.
    if (err.status === 403)
      return {
        tone: "denied",
        status: err.status,
        headline: t("settings.error.writeRefused.headline"),
        detail: translateDetail(err.message),
      };

    if (err.status >= 400 && err.status < 500)
      return {
        tone: "error",
        status: err.status,
        headline: t("settings.error.badRequest.headline"),
        detail: translateDetail(err.message),
      };

    return {
      tone: "error",
      status: err.status,
      headline: t("settings.error.fault.headline"),
      detail: translateDetail(err.message),
    };
  }

  // Nothing answered. fetch() rejects with a TypeError carrying a message from the browser,
  // not from the device, and the difference has to be visible: there is no status, and
  // whatever text there is was written by Chrome.
  const raw = err instanceof Error ? err.message : typeof err === "string" ? err : "";
  // Through translateDetail() too, for symmetry with the branches above; a browser string it has
  // no template for (the usual "Failed to fetch") passes through unchanged.
  const browserSaid = raw !== "" ? translateDetail(raw) : t("settings.error.browserGaveNoReason");

  if (opts.disconnectExpected)
    return {
      tone: "pending",
      status: null,
      headline: t("settings.error.disconnectExpected.headline"),
      detail: t("settings.error.disconnectExpected.detail", { browserSaid }),
    };

  return {
    tone: "offline",
    status: null,
    headline: t("settings.error.offline.headline"),
    detail: t("settings.error.offline.detail", { browserSaid }),
  };
}

/**
 * An `Explanation` collapsed to one line, for a caller with room for only a fragment -- e.g. the
 * note under a State page control (`../state/State.tsx`'s `write()`), which prefixes whatever it
 * gets with "Not sent: ". The Settings page instead renders `headline` and `detail` as two
 * separate pieces of UI (`sections/Notice.tsx`: `<strong>` then a `<span>`), which is the reason
 * this collapsing function lives here rather than being `Explanation`'s only shape.
 *
 * `.detail` ALONE is right for most tones, because most of the device's own sentences already
 * are sentences -- comfoair_config_strerror() writes full ones. `"denied"` is the exception:
 * 401 and 403 answer with one bare word each, `{"error":"unauthorized"}` /
 * `{"error":"forbidden"}` (comfoair_http_gate.c:135-146), and the actionable half of the message --
 * what to DO about it -- lives in `headline` ("The device wants its web-interface password"),
 * which `.detail` alone would throw away. Composing every tone instead would pad the already-
 * complete sentences the other branches carry for no benefit, so this composes only "denied".
 *
 * The parenthetical join matches this file's own convention for folding a bare, non-sentence
 * fragment into a sentence (see the `disconnectExpected` and offline branches above, both
 * `"... (${browserSaid})"`), rather than inventing a second style for the same problem.
 */
export function oneLineFragment(e: Explanation): string {
  if (e.tone === "denied") return `${e.headline} (${e.detail})`;
  return e.detail;
}
