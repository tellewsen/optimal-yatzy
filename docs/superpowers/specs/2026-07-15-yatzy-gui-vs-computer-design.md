# Yatzy Desktop GUI — vs Computer Mode

## Context

The GUI (built in [2026-07-12-yatzy-desktop-gui-design.md](2026-07-12-yatzy-desktop-gui-design.md),
restyled in [2026-07-13-yatzy-gui-visual-redesign-design.md](2026-07-13-yatzy-gui-visual-redesign-design.md))
is currently a single-player advisory tool: you enter your own physical dice
rolls, the engine recommends a move, you act on it. This spec adds a second
mode where you play a full match against a computer opponent that always
plays the DP-optimal move, plus a settings toggle to hide the advisory
recommendation list if you want to play blind.

This also builds on the already-shipped sidecar streaming change (`getRecommendation`
now takes an optional `onProgress` callback and reports `"popcount P/15"`
solve progress live) and the "Warm Up Engine" button — the computer's
automatic turns reuse both directly, so a mid-match cold solve reports
progress the same way a human query does, and a pre-match warm-up avoids the
computer stalling on its first move.

## Scope

- **Two modes, chosen at New Game time**: **Solo** (today's existing
  behavior, unchanged) and **vs Computer** (new). No mode-switching mid-game
  — starting a new game always re-opens the mode picker.
- **In vs Computer mode, you always go first**, then turns alternate every
  time a category gets scored, for both players, until both scorecards are
  full.
- **The computer's dice are simulated by the app** (`Math.random`-backed,
  same mechanism as today's "Roll remaining dice" button) — there is no
  physical die for a computer opponent. The computer always takes the
  top-ranked (best expected-value) option the engine returns, at every
  decision point, no exceptions.
- **A global "show optimal moves" toggle** controls whether the
  recommendation list renders at all, for either player's turn. Default: on.
- **A "computer turn pacing" setting**: **Instant** (the whole turn resolves
  in one click) or **Step-by-step** (one click per roll/hold/reroll
  decision, mirroring how your own turn already works). Default: instant.
- Out of scope: saving/resuming a match across app restarts (matches
  today's existing in-memory-only limitation), more than one computer
  opponent, difficulty levels other than optimal, changing mode mid-match.

## Data model

A new `MatchState` wraps the existing, unmodified `GameState` (from
`state.ts` — every existing pure transition function: `setDice`,
`applyHold`, `scoreCategory`, `advanceReroll`, `rollRemaining`,
`allDiceValid`, `isGameComplete`, `totalScore`, `bonusEarned` — is reused
as-is for whichever side is acting; none of them change):

```ts
interface MatchState {
  mode: "solo" | "vsComputer";
  turn: "player" | "computer";   // whose turn it is; meaningless in solo
  player: GameState;
  computer: GameState | null;    // null in solo mode
}
```

`initialGameState()` (existing, untouched) seeds both `player` and
`computer`. Solo mode is the degenerate case: `mode: "solo"`,
`computer: null`, `turn` always `"player"` — every existing solo code path
keeps working by reading `match.player` where it used to read the bare
`GameState`.

New pure logic (mirrors `state.ts`'s style and gets the same kind of unit
tests — `initialGameState`, transitions, and comparisons are all pure
functions with no DOM/Tauri dependency):

- `initialMatchState(mode): MatchState`
- Turn switching: after a category is scored, if `mode === "vsComputer"`,
  flip `turn` to the other side; in solo mode `turn` never changes.
- `isMatchComplete(match): boolean` — both sides' scorecards full (or just
  the player's, in solo mode).
- `matchWinner(match): "player" | "computer" | "tie" | null` — compares
  `totalScore(player)` vs `totalScore(computer)` once `isMatchComplete`;
  `null` before completion or in solo mode.
- Per-category leader comparison: for a given category index, once *both*
  sides have a non-null score there, which side's score is higher (or tied)
  — used purely for the highlight color, computed on the fly from the two
  `GameState.categoryScores` arrays rather than stored.

## New Game flow

Clicking "New Game" opens a centered modal dialog with two buttons: "Solo"
and "vs Computer". Picking one closes the dialog and starts a fresh
`MatchState` in that mode. This replaces the current behavior where "New
Game" immediately resets state with no prompt — solo-mode users now see one
extra click.

## Computer's turn

The computer's turn drives the *exact same* `getRecommendation` sidecar
call the human's turn already uses — the only difference is that it's
called in a loop instead of waiting for clicks, and it always acts on the
top-ranked (index 0) option:

```
loop:
  dice = rollRemaining(computer.dice)              // random fill, existing fn
  result = await getRecommendation(computer, dice, onProgress)
  if result.isRerollDecision:
      computer = applyHold(computer, result.rerollOptions[0].holdValues)
      continue
  else:
      computer = scoreCategory(computer, result.categoryOptions[0].category,
                                result.categoryOptions[0].resultingScore)
      break   // turn ends, MatchState flips turn to "player"
```

**Instant pacing**: a "Play Computer's Turn" button runs the whole loop to
completion, then renders the final outcome (what it rolled, which category
it scored, points earned) in one update.

**Step-by-step pacing**: the loop pauses after each iteration; a "Next"
button advances one roll/hold/reroll at a time. If "show optimal moves" is
on, each step also shows the recommendation list the computer is about to
act on (read-only — no click targets, since the computer's choice is
automatic) before advancing.

Both pacing modes reuse the existing `renderComputing`/progress-callback
plumbing for the "solving…" state during any individual sidecar call within
the loop — a cold cache mid-computer-turn behaves identically to a cold
cache mid-human-turn.

## Layout (vs Computer mode)

Top row: your scorecard card and the computer's scorecard card, side by
side (each narrower than today's single full-width scorecard card). A
"Your turn" / "Computer's turn" label sits above the turn panel, which
spans full width underneath and is simply retargeted at whichever side is
currently acting (same dice/rerolls/recommendation-list machinery already
built, just fed the active side's `GameState`). Solo mode's layout
(scorecard | turn panel, side by side) is unchanged.

Per-category score rows get a highlight color once *both* sides have
filled that category: the higher score's cell in each scorecard gets a
"leading" tint; the lower gets a neutral/muted tint; a tie gets neither.
Before both sides have played a category, no highlight (nothing to compare
yet).

## Settings panel

A gear icon next to "New Game" opens a small panel with two toggles: "Show
optimal move suggestions" (default on) and "Computer plays: Instant /
Step-by-step" (default instant). Both apply immediately, mid-game, no
confirmation — flipping "show optimal moves" off simply stops rendering the
recommendation-list area (for whichever side's turn is showing) until
turned back on; flipping computer pacing only affects the *next* computer
decision point, not one already in progress.

## End of match

Once `isMatchComplete` is true: in solo mode, the existing gold "🎉 Game
complete!" banner (unchanged). In vs Computer mode, a banner showing both
final totals and the winner, e.g. "You: 245 — Computer: 231 — You win! 🎉"
(or "It's a tie!"), styled consistently with the existing final-banner
look.

## Error handling

Unchanged from the existing single-player error path: any sidecar failure
(spawn error, non-zero exit, malformed JSON) shows the existing inline
error banner, whether the call was on behalf of the human or the computer.
A computer-turn loop that hits an error stops mid-turn and surfaces the
same error banner; there is no separate retry mechanism, matching the
existing "personal tool, one error message is enough" stance.

## Testing

- **New pure-logic unit tests** (same style as `state.test.ts`, no
  DOM/Tauri dependency): `initialMatchState`, turn-switching after
  `scoreCategory`, `isMatchComplete`, `matchWinner` (including the tie
  case), and the per-category leader comparison, covering solo mode as the
  degenerate case throughout.
- **No new tests for the computer-turn orchestration loop itself** — like
  the rest of the sidecar-calling code, it isn't unit-testable without a
  real Tauri runtime (`Command.spawn` requires it). Covered by manual
  verification instead.
- **Manual verification** (extends the existing "play a full game by hand"
  bar): play a full vs-Computer match in both pacing modes, toggle "show
  optimal moves" off and on mid-match, confirm turn alternation, per-category
  highlight coloring, and the winner banner (including forcing a tie by
  playing to matching totals, if practical, or at minimum confirming a
  clear win/loss renders correctly).

## Out of scope for this spec

- Persisting a match across app restarts.
- Any computer difficulty weaker than fully optimal play.
- More than two players / more than one computer opponent.
- Changing mode or opponent mid-match.
