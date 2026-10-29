# EmbedIDS documentation design

## Authority and scope

This is a documentation migration, inheriting the current Linuscent website's
identity from `~/linuscent/linuscent-website/src/styles/global.css` — including its
"void" vocabulary: HUD grid, scanlines, film grain, corner brackets, cyan glow,
magnetic buttons and entrance motion. The user confirmed dark/cyan as the primary
appearance, retaining a light-mode option. Two surfaces:

- **Overview (`docs/index.mdx`, Persuade):** a full-width splash landing page.
- **Every other page (Read):** Starlight's reading layout; the world owns the frame
  (masthead, sidebar, grid, titles, code and figures), the reading column stays calm.

## Direction contract

- **Thesis:** show EmbedIDS doing its job before explaining it. The overview leads
  with a live device monitor covering both device trust and intrusion detection; the
  docs frame carries Linuscent's HUD world.
- **Own-world:** near-black void, cyan signal, pink alert (`#f74d67`), Space Grotesk
  display, Inter text, JetBrains Mono for code, values and API names; hairline
  grids and corner brackets as the one structural motif.
- **Story:** watch a device's trust checks and intrusion detectors catch the
  case-study attacks → see the platforms it runs on → pick a path (Quick Start,
  tutorial, examples, case study) → the Zephyr RTOS module highlight → features,
  rationale, steps, use cases → get the SDK.
- **First viewport:** left, the headline ("Device trust and intrusion detection for embedded systems.", the last two words in cyan), tagline,
  Quick Start and GitHub actions, a fact strip (including "Zephyr RTOS module"); right
  (from 80rem; below, under the copy), the device monitor, labelled simulated, with
  scenario and pause controls. A "Runs on" platform belt spans the bottom of the hero.
- **Form:** user-pinned Linuscent identity; user chose a scenario carousel of shipped
  trust checks plus intrusion detection, and subtle motion for reading pages.
- **Finish:** verify desktop (1440/1920/2560), mobile, both themes, reduced motion,
  pause, keyboard focus, search, tabs, copy, zoom, consent, and preserved URLs.

## Tokens

| Role | Dark | Light |
| --- | --- | --- |
| Ground | `#05060a` | `#ffffff` |
| Sidebar surface | `#0a0c14` | `#f4f5f7` |
| Masthead surface | `#05060a` at 78%, blurred | `#ffffff` at 85%, blurred |
| Heading | `#e5e7eb` | `#0e0c1b` |
| Secondary text | `#adb4c2` | `#45434f` |
| Interactive text accent | `#22d3ee` | `#b92143` |
| Button/accent fill | `#22d3ee` | `#c32648` |
| Active fill | `#082e37` | `#fde7ec` |
| Alert (monitor, closing band) | `#f74d67` | same — these bands stay dark |
| Grid line | cyan at 6% | pink at 5% |

Light-mode pink is darkened from Linuscent's `#f74d67` for readable text on white.
The hero band, monitor, Zephyr panel and closing band stay "void" in both themes, as on
linuscent.com. Tokens live in `styles/brand.css`; landing layout in `styles/home.css`.

Large screens follow linuscent.com: the root font size rises to 16.5px (≥1536px),
17.5px (≥1920px) and 19px (≥2560px), and the reading column grows from 48rem to
52rem, so everything rem-based scales. The splash stage is 84rem.

## Motion

- **Focal moment:** `components/home/DeviceTrustMonitor.astro` — a simulated
  Jetson-class device. Four trust checks (model, config and agent-binary integrity,
  telemetry heartbeat) and four intrusion detectors (GPU/CPU, inference, processes,
  network) feed the EmbedIDS engine along SVG wires with travelling packets. Two
  status pills keep **Device trust** and **Intrusion** separate. A carousel plays the
  case-study scenarios (GPU hijack, model tampering, data exfiltration, telemetry
  loss), about 8.5 s each: tiles change state, the engine correlates and raises an
  incident, and alerts enter the feed. Every alert title, hash, value and detection
  time is taken from `docs/case-studies/edgeml-runtime-security.mdx`; only shipped
  checks are shown (no secure boot or TPM until they ship). Scenario buttons jump
  directly; pauses off-screen, in hidden tabs and via its button. Reduced motion shows
  each scenario's final state with no movement.
- **Zephyr case study:** `components/case-studies/ZephyrReplay.astro` replays one
  recorded `scripts/zephyr/run_scenarios.py` run: commands type into a UART line,
  application threads feed a 32-cell `k_msgq`, each slowed-down 100 ms cycle drains
  into eight rule meters, and alert JSON lines wipe in. Values, lines and latencies
  come from `scripts/zephyr/case-study-data/`; seven scenario buttons, pause,
  off-screen/hidden-tab pausing, and reduced motion showing each final state.
  `DetectionBudget.astro` draws latency bars against the 200 ms budget once on
  scroll (only under `motion-ok`).
- **Platform belt:** `components/home/PlatformMarquee.astro` scrolls a duplicated list
  (the copy is `aria-hidden` and `inert`) at 46 s per loop. It pauses on hover, focus
  and its Pause button; reduced motion shows one static wrapped list. Each platform
  states its validation level (for example "Collectors built · hardware next").
- **Zephyr highlight:** `components/home/ZephyrHighlight.astro`, a void panel after the
  start paths; its 16-cell queue fills and drains with the detector pulse and an alert
  line wipes in, on a 3.2 s CSS loop that stops under reduced motion.
- **Entrances:** hero copy rises and the monitor "powers on" (clip + brightness);
  homepage sections reveal on scroll; key-feature rules and the getting-started rail
  draw in; page titles draw a short signal line. Documentation images are not
  animated: they render with the page (a scan-in reveal felt laggy).
- **Feedback:** sidebar and path tiles gain HUD corner brackets; code frames light up
  on hover/focus; table rows highlight; buttons lean towards the pointer.
- Hidden-until-revealed states apply only under `:root.motion-ok`, set in the head
  when motion is allowed and removed after 3s if the reveal script never runs.

## Preserved URLs (SEO and shared links)

The previous MkDocs site's addresses are a contract, enforced in CI by
`scripts/compare-urls.mjs` against the published `gh-pages` branch:

- page URLs, canonicals, titles (`Page - EmbedIDS`, via `titleDelimiter`) and
  descriptions are unchanged;
- every heading anchor keeps its ID *and* its heading text; former page-title IDs
  (e.g. `#quick-start`) are kept with the `legacyAnchors` frontmatter field — do
  not add headings that would take an existing ID (the case-study summary uses
  cards for this reason);
- `docs/assets/**` is copied to `<version>/assets/**` so figures, logos, favicon and
  share cards keep their URLs; each page's `og:image` is its original card in
  `docs/assets/images/social/`;
- `<version>/sitemap.xml` (+ `.gz`) is the URL set, listed by the root sitemap index.

## Asset provenance

All logos, charts, dashboard screenshots and the five social cards predate this
migration (the cards were rendered by the MkDocs social plugin) and remain in
`docs/assets/`. No generated artwork was introduced; the device monitor is drawn
live from a synthetic signal and labelled as simulated. Recorded telemetry charts
are owned by `edgeml/tools/case_study_charts.py` and must not be hand-edited.
`docs/assets/case-studies/zephyr/hero.svg` is authored for the case-study index; its
figures come from `scripts/zephyr/case-study-data/` and its loop stops under reduced
motion. `docs/assets/images/social/case-studies/zephyr-runtime-security.png` was
rendered with Playwright to match the MkDocs-era cards (Inter, white ground, chip logo).
Platform marks in `website/assets/platforms/` are Simple Icons 16.36.0 (CC0) for Linux,
NVIDIA, Docker, CMake and C, and the official dark-background Zephyr logo from the
Zephyr v4.2.0 repository (`doc/_static/images/logo-readme-dark.svg`). They are
trademarks of their owners, shown to state compatibility; keep them unaltered apart
from the monochrome Simple Icons fill.
Fontsource packages self-host the same font families as the Linuscent website.

## Maintenance

Keep framework overrides small: `Header` (top menu, data in `data/nav.ts`), `Hero`,
`PageTitle`, `Head`, `Footer`, `SiteTitle`, `ThemeProvider` and `MobileMenuToggle`.
The top menu shows from 64rem; below that, documentation pages use the sidebar
menu and the overview gets its own full-screen menu. Keep `data/nav.ts` and the
`sidebar` in `astro.config.mjs` in step. Never put `backdrop-filter` (or `transform`)
on the header itself: it would trap fixed menus inside it, so the blur lives on
`header.header::before`. Styles for dynamically created elements
(monitor log lines, zoomed images) must be `:global(...)` because Astro scopes CSS
by attribute. The mobile menu override sets `aria-expanded` on both the button and
its host element, which Starlight's CSS uses to reveal the sidebar. Analytics remain
consent-gated. Check both themes, reduced motion and narrow widths when adding styles.
