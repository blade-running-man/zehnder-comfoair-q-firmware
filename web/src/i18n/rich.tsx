import type { ComponentChildren, JSX } from "preact";

/**
 * A translated sentence with markup inside it: `template` is a catalog string already passed
 * through t(), and every `{name}` left in it that `nodes` names is replaced by that node
 * (typically a <strong>/<b> around a reading). t() leaves an unknown {token} verbatim, so a
 * caller passes its plain-text params to t() and its markup params here.
 *
 * WHY a split and not separate keys per fragment: a sentence cut into "Outside it is" + value +
 * "and the bypass is open" fixes the English word order into every language; one template with
 * a placeholder lets German or Ukrainian move the reading wherever its grammar wants it.
 */
export function rich(template: string, nodes: Record<string, ComponentChildren>): JSX.Element {
  const parts = template.split(/\{(\w+)\}/g);
  // split() with one capture group alternates text, token, text, token, ... text.
  return (
    <>
      {parts.map((part, i) => (i % 2 === 1 ? (part in nodes ? nodes[part] : `{${part}}`) : part))}
    </>
  );
}
