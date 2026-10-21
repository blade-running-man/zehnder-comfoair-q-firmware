// App-wide action and state words, shared by every page. A page that needs a word only it
// uses keeps it in its own namespace; one added here must be added in all four languages.
import type { Message } from "../../format.ts";

export const common = {
  "common.set": "Set",
  "common.save": "Save",
  "common.saving": "Saving…",
  "common.cancel": "Cancel",
  "common.apply": "Apply",
  "common.close": "Close",
  "common.retry": "Retry",
  "common.refresh": "Refresh",
  "common.loading": "Loading…",
  "common.yes": "Yes",
  "common.no": "No",
  "common.on": "On",
  "common.off": "Off",
  "common.unknown": "Unknown",
  "common.error": "Error",
} satisfies Record<string, Message>;
