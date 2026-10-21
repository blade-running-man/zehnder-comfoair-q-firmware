// The language switcher, mounted in Nav so it is part of every page's chrome.
//
// It reads `locale.value` in render, so it re-renders on a switch, and writes through
// setLocale, which is the one place persistence and <html lang> are kept in step. DO NOT
// assign `locale.value` directly from here: the choice would not survive a reload and
// <html lang> would disagree with the page.

import { Select } from "../ui/Select";
import { LOCALES, locale, setLocale, t, type Locale } from "../../i18n";

export function LangSwitcher() {
  return (
    <Select
      ariaLabel={t("nav.language")}
      value={locale.value}
      options={LOCALES.map((l) => ({ value: l.code, label: l.short }))}
      onChange={(v) => setLocale(v as Locale)}
    />
  );
}
