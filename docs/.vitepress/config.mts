import { defineConfig, type DefaultTheme } from 'vitepress'
// The functional areas of the "Ventilation unit" section, in the order the annotations list them.
// Read from the same file the pages render, so a renamed or added area cannot leave the sidebar
// behind.
import unitAnnotations from '../unit/data/annotations.json'

// The site root is docs/. One path is deliberately NOT part of the site:
//   manuals/      -- source material kept beside the docs, not authored pages.
// Links into it from site pages are real links in the repository (GitHub renders them), so they
// are allowed to dangle here -- see ignoreDeadLinks below. Nothing else is.

// A relative link whose target resolves outside docs/. Every site page sits at most one directory
// deep (docs/<section>/<page>.md), so two leading `../` always leave docs/; a root page
// (index.md) cannot be told apart from a section page here because VitePress hands the predicate
// the link alone, not its source file -- root pages must therefore not link `../` into the repo.
// VitePress normalises relative links to a leading `./` before the check, hence the optional prefix.
const LEAVES_DOCS = /^(\.\/)?(\.\.\/){2,}/
// A link into the excluded manuals/ tree, from any depth.
const INTO_EXCLUDED = /(^|\/)manuals\//

// The "Functional areas" group: every area of annotations.json in its order, except that the
// catch-all `other` area goes last and is titled "Other units" -- it holds the units that answered
// "unknown unit" and the present ones nobody has identified, which is not a function of the unit.
function unitAreaItems(): DefaultTheme.SidebarItem[] {
  const areas = Object.entries(unitAnnotations.areas)
  const ordered = [...areas.filter(([slug]) => slug !== 'other'), ...areas.filter(([slug]) => slug === 'other')]
  return ordered.map(([slug, area]) => ({
    text: slug === 'other' ? 'Other units' : area.title,
    link: `/unit/areas/${slug}`,
  }))
}

const sidebar: DefaultTheme.SidebarMulti = {
  '/guide/': [
    {
      text: 'Guide',
      items: [
        { text: 'Introduction', link: '/guide/introduction' },
        { text: 'Installation', link: '/guide/installation' },
        { text: 'First setup', link: '/guide/first-setup' },
        { text: 'Configuration', link: '/guide/configuration' },
        { text: 'Home Assistant', link: '/guide/home-assistant' },
        { text: 'Updating', link: '/guide/updating' },
        { text: 'Troubleshooting', link: '/guide/troubleshooting' },
      ],
    },
  ],
  '/architecture/': [
    {
      text: 'Architecture',
      items: [
        { text: 'Overview', link: '/architecture/overview' },
        { text: 'Components', link: '/architecture/components' },
        { text: 'Runtime', link: '/architecture/runtime' },
        { text: 'Failure model', link: '/architecture/failure-model' },
        { text: 'Security', link: '/architecture/security' },
      ],
    },
  ],
  '/protocol/': [
    {
      text: 'Protocol',
      items: [
        { text: 'CAN bus', link: '/protocol/can-bus' },
        { text: 'PDO and RMI', link: '/protocol/pdo-rmi' },
        { text: 'Polling', link: '/protocol/polling' },
        { text: 'Commands', link: '/protocol/commands' },
        { text: 'Probe', link: '/protocol/probe' },
      ],
    },
  ],
  '/interfaces/': [
    {
      text: 'Interfaces',
      items: [
        { text: 'REST API', link: '/interfaces/rest-api' },
        { text: 'WebSocket', link: '/interfaces/websocket' },
        { text: 'MQTT', link: '/interfaces/mqtt' },
        { text: 'Home Assistant discovery', link: '/interfaces/home-assistant-discovery' },
        { text: 'Web UI', link: '/interfaces/web-ui' },
      ],
    },
  ],
  '/device/': [
    {
      text: 'Device',
      items: [
        { text: 'Board', link: '/device/board' },
        { text: 'Indication', link: '/device/indication' },
        { text: 'Button', link: '/device/button' },
        { text: 'Networking', link: '/device/networking' },
        { text: 'OTA and system', link: '/device/ota-and-system' },
        { text: 'Configuration storage', link: '/device/config-storage' },
      ],
    },
  ],
  '/development/': [
    {
      text: 'Development',
      items: [
        { text: 'Building', link: '/development/building' },
        { text: 'Testing', link: '/development/testing' },
        { text: 'Entity registry', link: '/development/entity-registry' },
        { text: 'Conventions', link: '/development/conventions' },
        { text: 'Releasing', link: '/development/releasing' },
      ],
    },
  ],
  '/unit/': [
    {
      text: 'Ventilation unit',
      items: [{ text: 'Interface', link: '/unit/' }],
    },
    {
      text: 'Functional areas',
      items: unitAreaItems(),
    },
    {
      text: 'Reference',
      items: [
        { text: 'Unit map', link: '/unit/units' },
        { text: 'Property reference', link: '/unit/properties' },
      ],
    },
  ],
  // The owner's open hardware test scenarios, in the order they are walked.
  '/hardware-verification/': [
    {
      text: 'Hardware verification',
      items: [
        { text: 'Overview', link: '/hardware-verification/' },
        { text: '00 · Setup and safety', link: '/hardware-verification/00-setup-and-safety' },
        { text: '01 · Boot and resilience', link: '/hardware-verification/01-boot-and-resilience' },
        { text: '02 · CAN bus and polling', link: '/hardware-verification/02-can-bus-and-polling' },
        { text: '03 · Web, API and access', link: '/hardware-verification/03-web-api-and-access' },
        { text: '04 · Commands', link: '/hardware-verification/04-commands' },
        { text: '05 · Provisioning and recovery', link: '/hardware-verification/05-provisioning-and-recovery' },
        { text: '05b · Network outages', link: '/hardware-verification/05b-network-outages' },
        { text: '06 · MQTT', link: '/hardware-verification/06-mqtt' },
        { text: '06b · Home Assistant', link: '/hardware-verification/06b-home-assistant' },
        { text: '07 · Indication and button', link: '/hardware-verification/07-indication-and-button' },
        { text: '08 · OTA and system', link: '/hardware-verification/08-ota-and-system' },
        { text: '09 · RMI probe', link: '/hardware-verification/09-rmi-probe' },
      ],
    },
  ],
  '/reference/': [
    {
      text: 'Reference',
      items: [
        { text: 'Overview', link: '/reference/' },
        { text: 'Entities', link: '/reference/entities' },
        { text: 'Smart-home standards', link: '/reference/smart-home-standards' },
      ],
    },
    {
      // The same documents, in the same order, as reference/index.md. They keep their paths
      // because code comments cite them by path and section.
      text: 'Protocol and device',
      collapsed: false,
      items: [
        { text: 'PDO & RMI reference', link: '/reference/comfoair-pdo-rmi' },
        { text: 'RMI probe findings (Q350)', link: '/reference/rmi-probe-findings-q350' },
        { text: 'ComfoAir Q hardware', link: '/reference/comfoair-q-hardware' },
        { text: 'ESP32 nodes and CAN transceivers', link: '/reference/esp32-node-hardware' },
        { text: 'This device: exact figures', link: '/reference/hardware' },
      ],
    },
  ],
}

export default defineConfig({
  lang: 'en-US',
  title: 'ComfoAir Q Firmware',
  description:
    'Own ESP-IDF firmware for the Zehnder ComfoAir Q: CAN, MQTT with Home Assistant discovery, REST and a web UI over one model.',

  // The release workflow may publish the site under a sub-path (e.g. GitHub Pages /<repo>/).
  base: process.env.DOCS_BASE ?? '/',
  // Separate dirs per build so parallel builds (several agents at once) never share state.
  ...(process.env.DOCS_CACHE_DIR ? { cacheDir: process.env.DOCS_CACHE_DIR } : {}),
  ...(process.env.DOCS_OUT_DIR ? { outDir: process.env.DOCS_OUT_DIR } : {}),

  srcExclude: [
    'manuals/**',
    '**/node_modules/**',
  ],

  // Dark first, like the device's own web UI the site is styled after (theme/cyber.css); the
  // toggle still switches to the light variant and remembers the reader's choice.
  appearance: 'dark',
  head: [
    ['meta', { name: 'theme-color', content: '#0a0a0f', media: '(prefers-color-scheme: dark)' }],
    ['meta', { name: 'theme-color', content: '#f4f5fa', media: '(prefers-color-scheme: light)' }],
  ],

  cleanUrls: true,
  lastUpdated: false,

  // DO NOT set this to `true`: a dead link between site pages is a real bug. Only links that
  // genuinely point outside the built site are let through.
  ignoreDeadLinks: [
    INTO_EXCLUDED,
    LEAVES_DOCS,
    // Two source documents under reference/ cite a web-ui-design.md that is not part of this site.
    // They are kept verbatim because code comments cite them by path.
    './web-ui-design',
  ],

  themeConfig: {
    // Five top-level entries, not one per section: nine did not fit a laptop-width bar. The
    // sections behind a dropdown keep their own sidebars, so nothing is harder to reach once inside.
    nav: [
      { text: 'Guide', link: '/guide/introduction', activeMatch: '^/guide/' },
      {
        text: 'Firmware',
        activeMatch: '^/(architecture|interfaces|protocol|device)/',
        items: [
          { text: 'Architecture', link: '/architecture/overview', activeMatch: '^/architecture/' },
          { text: 'Interfaces', link: '/interfaces/rest-api', activeMatch: '^/interfaces/' },
          { text: 'Protocol', link: '/protocol/can-bus', activeMatch: '^/protocol/' },
          { text: 'Device', link: '/device/board', activeMatch: '^/device/' },
        ],
      },
      { text: 'Ventilation unit', link: '/unit/', activeMatch: '^/unit/' },
      {
        text: 'Development',
        activeMatch: '^/(development|hardware-verification)/',
        items: [
          { text: 'Building, testing, releasing', link: '/development/building', activeMatch: '^/development/' },
          { text: 'Hardware verification', link: '/hardware-verification/', activeMatch: '^/hardware-verification/' },
        ],
      },
      { text: 'Reference', link: '/reference/', activeMatch: '^/reference/' },
    ],
    sidebar,
    outline: [2, 3],
    search: { provider: 'local' },
  },
})
