import { useEffect, useState } from "preact/hooks";
import { getLog } from "../../api/client";
import { Button } from "../../components/ui";
import { describeEmpty, levelOf } from "./log.ts";
import { t } from "../../i18n";
import styles from "./Log.module.css";

/**
 * The device's own log, oldest line first.
 *
 * NOT live. The ring is a snapshot fetched on demand: it holds eighty lines and the push socket
 * carries state, not text. A page that polled it would spend the device's single HTTP task on a
 * screen nobody is usually watching.
 */
export function LogPage(_props: { path?: string }) {
  const [lines, setLines] = useState<string[] | null>(null);
  const [read, setRead] = useState(true);
  const [loading, setLoading] = useState(false);

  async function load() {
    setLoading(true);
    try {
      setLines(await getLog());
      setRead(true);
    } catch {
      setLines([]);
      setRead(false);
    }
    setLoading(false);
  }

  useEffect(() => {
    void load();
  }, []);

  return (
    <div class={styles.page}>
      <div class={styles.header}>
        <h2>{t("log.title")}</h2>
        <Button onClick={load} loading={loading}>
          {t("common.refresh")}
        </Button>
      </div>

      {lines === null ? (
        <p>{t("common.loading")}</p>
      ) : lines.length === 0 ? (
        <p>{describeEmpty(read)}</p>
      ) : (
        <pre class={styles.log}>
          {lines.map((line, i) => (
            <div key={i} class={styles[levelOf(line)]}>
              {line}
            </div>
          ))}
        </pre>
      )}
    </div>
  );
}
