// Small display helpers shared by the unit components.

export const DASH = '—'

export const hex2 = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(2, '0')}`

/** A source as short link text: a GitHub blob URL becomes "<repo>/<file>#L<n>", a path stays. */
export function sourceLabel(src: string): string {
  const m = src.match(/^https:\/\/github\.com\/[^/]+\/([^/]+)\/blob\/[^/]+\/(?:.*\/)?([^/#]+)(#L\d+)?$/)
  if (m) return `${m[1]}/${m[2]}${m[3] ?? ''}`
  return src
}

export const isUrl = (src: string): boolean => /^https?:\/\//.test(src)

export function writableText(w: boolean | null): string {
  return w === true ? 'yes' : w === false ? 'no' : DASH
}
