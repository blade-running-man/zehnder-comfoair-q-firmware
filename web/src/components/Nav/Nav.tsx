import { route } from "preact-router";
import { signal } from "@preact/signals";
import { ConnectionBadge } from "../ConnectionBadge";
import { LangSwitcher } from "../LangSwitcher";
import { t } from "../../i18n";
import styles from "./Nav.module.css";

const currentPath = signal(window.location.pathname);
const theme = signal<"dark" | "light">(
  (localStorage.getItem("theme") as "dark" | "light") || "dark"
);
document.documentElement.setAttribute("data-theme", theme.value);

export function toggleTheme() {
  theme.value = theme.value === "dark" ? "light" : "dark";
  document.documentElement.setAttribute("data-theme", theme.value);
  localStorage.setItem("theme", theme.value);
}

window.addEventListener("popstate", () => {
  currentPath.value = window.location.pathname;
});

// Catalog KEYS, not labels: t() runs in render, so the links re-translate on a language switch.
// DO NOT resolve them here at module load -- that would freeze them in the first language.
const links = [
  { href: "/", key: "nav.home" },
  { href: "/diagnostics", key: "nav.diagnostics" },
  { href: "/capabilities", key: "nav.probe" },
  { href: "/system", key: "nav.system" },
  { href: "/settings", key: "nav.settings" },
  { href: "/log", key: "nav.log" },
  { href: "/update", key: "nav.update" },
] as const;

export function Nav() {
  const path = currentPath.value;

  function navigate(e: Event, href: string) {
    e.preventDefault();
    route(href);
    currentPath.value = href;
  }

  return (
    <nav class={styles.nav}>
      <strong class={styles.logo} data-text={"ComfoAir Q"}>ComfoAir&nbsp;Q</strong>
      {links.map((l) => (
        <a
          key={l.href}
          href={l.href}
          onClick={(e: Event) => navigate(e, l.href)}
          class={`${styles.link}${path === l.href ? ` ${styles.linkActive}` : ""}`}
        >
          {t(l.key)}
        </a>
      ))}
      <span class={styles.spacer} />
      <ConnectionBadge />
      <LangSwitcher />
      <button
        class={`theme-toggle ${styles.themeToggle}`}
        onClick={toggleTheme}
        title={t("nav.theme.toggle")}
        aria-label={
          theme.value === "dark" ? t("nav.theme.toLight") : t("nav.theme.toDark")
        }
      >
        {theme.value === "dark" ? "\u2600" : "\u263D"}
      </button>
    </nav>
  );
}
