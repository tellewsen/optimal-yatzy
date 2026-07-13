# Yatzy Desktop GUI — Visual Redesign

## Context

The GUI built in [2026-07-12-yatzy-desktop-gui-design.md](2026-07-12-yatzy-desktop-gui-design.md)
is functionally complete (full turn-by-turn play against the DP engine) but
visually bare: unstyled default `<button>`/`<input type="number">` elements,
no color, single stacked column. This spec covers a purely visual/interaction
redesign — no change to game logic, state model, sidecar protocol, or test
coverage. `state.ts`, `sidecar.ts`, `parseResult.ts`, `cliArgs.ts` are
untouched; only `render.ts`, `styles.css`, `index.html`, and `main.ts`'s
event wiring (to support the new dice-cycling interaction) change.

## Direction

Playful/game-like, "warm felt-table" theme: a tabletop-game feel rather than
a plain data-entry form, since this is used casually while physically playing
Yatzy with real dice.

## Layout

Two-column layout inside the existing 800×600 window:

- **Header bar**: game title (left), "New Game" button (right-aligned).
- **Scorecard card** (left column, ~45% width): all 15 categories, upper
  subtotal + bonus progress, grand total — same data as today, restyled as a
  card.
- **Turn card** (right column, ~55% width): dice row, rerolls indicator,
  "Roll remaining dice" button, "Reroll →" button, recommendation list, error
  banner — same data as today, restyled as a card.
- Both cards sit on a felt-green page background.
- On game completion, a banner overlays spanning both cards (see
  "Final total" below) rather than a plain text line underneath.

Below ~700px window width the two columns stack vertically (cards keep their
own width constraints via flex-wrap; no separate breakpoint logic needed
given this app's window is fixed-size in practice).

## Color system

| Role | Value |
|---|---|
| Page background | `#1b5e3a`, subtle radial gradient to `#164a2e` at edges |
| Card background | `#fdf8ec` |
| Card border | `#e6dcc3`, 1px, plus soft brown-tinted `box-shadow` |
| Primary text (on cards) | `#2b2016` |
| Header text (on felt) | `#fdf8ec` |
| Primary action buttons (Roll remaining, Reroll →, New Game) | background `#b3273e`, text `#fdf8ec` |
| Best-move highlight | border `#d4a017`, "★ Best move" badge, background tint `#fbf0d4` |
| Error banner | background `#b00020`, text `#fdf8ec`, rounded corners |
| Rerolls-left dots (filled/used) | filled `#2b2016`, used/hollow `#cfc4a3` |

## Typography

System rounded-font stack, no network font loading (app must work fully
offline): `"Segoe UI Rounded", "SF Pro Rounded", system-ui, sans-serif`.
Headings (card titles, header bar title) slightly bolder/larger; scorecard
category names use a slightly condensed weight so the narrower left column
stays compact without wrapping.

## Components

### Pip dice (replaces `<input type="number">`)

Each of the 5 dice is a ~64px cream rounded-square button showing pips laid
out via CSS grid in the standard 6 face patterns (1 centered; 2 diagonal; 3
diagonal+center; 4 corners; 5 corners+center; 6 two columns of 3), black
pips on the cream face, subtle inset shadow for a tactile look.

- **Empty state** (`dice[i] === null`): dashed-border blank square, no pips.
- **Interaction**: click, or `Enter`/`Space` while focused, cycles the die's
  value 1→2→3→4→5→6→1→… (wraps). An empty die starts at 1 on first
  click/keypress. There is no way to clear a die back to empty via the pip
  UI — clearing only happens via game-state transitions (reroll, hold,
  score, new game), matching current behavior where dice are only ever
  programmatically cleared, never user-cleared mid-entry.
- Each die is a native `<button type="button">` (not `<input>`) for built-in
  keyboard focus/activation semantics; `main.ts` wires a `click` listener
  that computes `next value = value === null ? 1 : (value % 6) + 1` and
  calls the existing `onChange(index, value)` callback — `renderDiceInputs`'s
  signature and the `handleDiceChange`/`maybeQuery` flow in `main.ts` are
  otherwise unchanged.

### Rerolls-left indicator

Replaces the `Rerolls left: N` text with 2 small dot icons next to the "Roll
remaining dice" button: filled dot = reroll available, hollow/muted dot =
reroll used. `rerollsLeft` (0, 1, or 2) maps directly to how many dots are
filled.

### Recommendation list

Each reroll-hold option or category option renders as a card-style row
(padded, rounded, subtle border) instead of a plain button — same click
behavior and same text content as today (hold values / expected value, or
category name / score / expected value). Since `parseResult` already returns
options best-first, the **first item in each list** gets the gold
"Best move" treatment (gold border, tinted background, "★ Best move" badge);
remaining items get a plain cream row with a hover state.

### Computing state

`renderComputing()` keeps its explanatory text but is shown alongside a
small pulsing/spinning die glyph (CSS animation on a pip-die shape), so a
multi-minute cold-cache solve doesn't look frozen.

### Final total

`renderFinalTotal` changes from a plain text line to a gold ribbon-style
banner ("🎉 Game complete! Final score: N") positioned to visually span both
cards when `isGameComplete(state)` is true. Same visibility logic
(`hidden`/shown) as today.

### Error banner

Same show/hide logic as today (`renderError`), restyled: rounded corners,
padding, `#b00020` background — a visual restyle only, no behavior change.

## Out of scope

- Any change to `state.ts`, `sidecar.ts`, `parseResult.ts`, `cliArgs.ts`, or
  their existing unit tests — this is a rendering/interaction-layer change
  only.
- Dark mode / theme switching.
- Sound effects or dice-roll animations beyond the computing-state pulse.
- Responsive/mobile layout beyond the simple flex-wrap fallback described
  above (this is a fixed-size Windows desktop app).

## Testing

No new pure-logic to unit test (the dice-cycling math is a one-line
transform inside an event listener, not a new pure function worth
extracting/testing separately). Verification is manual: run `npm run tauri
dev` on Windows and play through entering dice via the new pip-click
interaction, triggering a reroll-options view and a category-options view,
confirming the best-move highlight lands on the first (best) item in each,
and completing a full game to see the final-score banner — same manual
verification bar as the original GUI spec, extended to cover the new
interaction.
