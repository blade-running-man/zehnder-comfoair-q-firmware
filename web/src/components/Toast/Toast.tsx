import { signal } from "@preact/signals";

// A queue, not a single slot: "Config saved" immediately followed by "Error saving"
// must both be seen. A lone signal + shared timer let the second call overwrite the
// first and reset its clock, so the earlier message vanished. Each toast now owns its
// own id and timeout and is removed independently.
interface ToastItem {
  id: number;
  msg: string;
}

const toasts = signal<ToastItem[]>([]);
let nextId = 0;

export function toast(msg: string, ms = 2000) {
  const id = nextId++;
  toasts.value = [...toasts.value, { id, msg }];
  setTimeout(() => {
    toasts.value = toasts.value.filter((t) => t.id !== id);
  }, ms);
}

export function ToastContainer() {
  // The region is always mounted so its aria-live is armed before a toast arrives.
  // role="status"/aria-live="polite" is what carries the message to a screen reader --
  // a fixed-position div that only appears on success is invisible to one otherwise.
  return (
    <div class="toast-region" role="status" aria-live="polite">
      {toasts.value.map((t) => (
        <div key={t.id} class="toast">
          {t.msg}
        </div>
      ))}
    </div>
  );
}
