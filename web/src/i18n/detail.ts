// Best-effort translation of a firmware error detail sentence; English passthrough on no match
// (firmwareText.ts says why there is no stable machine code to key off instead).
//
// This wrapper -- not firmwareText.ts itself -- imports t(), because t() reads the reactive
// locale signal (i18n/index.ts, @preact/signals) and firmwareText.ts must stay pure and
// node-safe.
import { matchTemplate, DETAIL_TEMPLATES } from "./firmwareText.ts";
import { t } from "./index.ts";

export function translateDetail(message: string): string {
  const m = matchTemplate(message, DETAIL_TEMPLATES);
  return m ? t(m.key, m.params) : message;
}
