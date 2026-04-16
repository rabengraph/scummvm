# Benchmark Design — Game-Agnostic Progress Scoring

Status: **draft / proposal**. Not yet implemented.

This document is the harness-side benchmark proposal plus the fork-side
verification report it asks for. It lives in the fork because the
verification answers reference fork-internal `file:line` locations that
should travel with the engine source.

The runtime contract this benchmark sits on top of is described in
`engines/scumm/AGENT_HARNESS.md`. Read that first.

---

## Goal

Turn the Scummbar harness into a benchmark tool that measures how well an AI
agent plays ScummVM games — not just lets it play them.

## Core premise

The benchmark must be **game-agnostic**. It should not know that Monkey Island
has a rubber chicken or that Day of the Tentacle has a time machine. This is a
feature, not a weakness:

> An agent that scores higher across a suite of different SCUMM games is, by
> construction, generally better at playing SCUMM. If we hand-author
> per-game milestones, we are testing memorization of one title, not the
> agent's ability to make progress in adventure games at large.

Therefore the benchmark must be a **pure function of the telemetry stream**
that the fork already publishes — no per-game goal lists, no hand-authored
milestones, nothing that reads the game manual.

## The only generalizable KPI: progress

Across all SCUMM games the only shared success signal is *making progress*. We
cannot know **what** progress means in a given title, only that the world
state advanced. That is enough: we approximate progress as **state novelty**.

## Run budget

Runs are time-boxed. The user **declares a budget up front**, chosen from four
tiers:

- **5 minutes** — quick smoke test (can the agent orient at all?)
- **10 minutes** — early-game fluency
- **30 minutes** — mid-game exploration
- **1 hour** — deeper puzzle-chaining

Longer tiers are not considered for v1 — token cost climbs quickly and
diminishing-return scoring (see below) means very long budgets don't add much
signal.

The declared budget is part of the run fingerprint and is **the denominator
for all rate-based scoring**, regardless of how the run actually ends.

### Stopping a run

Three stop conditions, in priority order:

1. **Explicit stop** — the agent calls `__benchmarkStop("done")` or a human
   watcher clicks an End button. Score is computed on the events up to that
   point. This is the normal, graceful exit.
2. **Hard ceiling** — if the run exceeds the declared budget (wall-clock or
   action count), the runner force-stops it. Backstop for agents that don't
   self-terminate.
3. **Crash / disconnect** — run marked invalid, excluded from leaderboards.

Rate-based scoring against the **declared** budget (not elapsed time) keeps
the two ends of the spectrum honest:

- Early-quit gaming is neutralized: an agent that finds one novelty and
  quits at 30 seconds gets scored as `1 / 5min`, not `1 / 30sec`.
- Overtime gaming is impossible: the hard ceiling enforces the declared
  budget.

Stopping early is only neutral if the agent had genuinely plateaued.

## Novelty primitives

Each primitive is a monotonic set that only grows within a run. Adding an
element counts as one novelty event. All are derivable from the v1 telemetry
contract (`web/shared/bridge.js`, `web/shared/mock.js`, and the fork's
`engines/scumm/AGENT_HARNESS.md`).

| # | Primitive | Derived from | Notes |
|---|---|---|---|
| 1 | `Set<roomId>` rooms visited | `snapshot.room` | Strongest universal signal — adventure games gate progress on room access |
| 2 | `Set<(roomId, objectId)>` objects seen | `roomObjects[]` | Catches objects that appear mid-room via script triggers |
| 3 | `Set<(objectId, state)>` object-state transitions | `roomObjects[].state` | Door opened, box opened, lever pulled. Reopening a door is not re-rewarded |
| 4 | `Set<objectId>` inventory first-acquisitions | `inventory[]` | Drop/repick does not re-score |
| 5 | `Set<actorId>` actors encountered | `actors[]` | NPC discovery |
| 6 | `Set<msgTextHash>` unique lines heard | `messageStateChanged.text` | New dialog lines = new script paths reached. Covers the "information gathering" axis |
| 7 | `Set<(actorId, dialogChoiceId)>` dialog branches | `dialogChoicesChanged` | Reaching a new dialog node |
| 8 | Cutscene count | `inCutsceneChanged false→true` | Major plot beats — SCUMM fires cutscenes on milestones |

For v1 all primitives are weighted equally (weight = 1). The scoring function
should be structured so that per-primitive weights can be tuned later, but we
do not calibrate them now — that would be guessing.

## Scoring

### Primary score

Per run, maintain the cumulative novelty curve `N(x)` where `x` is elapsed
seconds. The primary per-run score blends **rate** (how good per unit time)
and **budget** (how long the agent committed to play):

```
rate_score = AUC(N(t)) / T_budget              // time-weighted avg novelty rate
run_score  = rate_score × sqrt(T_budget)       // reward longer budgets sublinearly
```

The `sqrt` term is intentional:

- A 1-hour run at half the rate of a 5-min run still beats it — longer
  commitment pays off.
- But the payoff is sublinear: 12x the budget yields only ~3.5x the
  multiplier. This prevents the benchmark from degenerating into "whoever
  rents the most compute wins," and matches the reality that SCUMM
  progress plateaus as puzzles get harder.

Worked example:

| Agent | Budget | Rate (novelty/min) | `rate × √T` |
|---|---|---|---|
| A | 5 min | 4 | 4 × √5 ≈ **8.94** |
| B | 60 min | 2 | 2 × √60 ≈ **15.49** ✓ |
| C | 60 min | 1 | 1 × √60 ≈ **7.75** ✗ (loses to 5-min agent) |

### Efficiency signals

Diagnostic metrics reported alongside the primary score. They capture the
three concerns from the design conversation (item-acquisition speed, room
thrashing, action repetition) and are cheap to compute from the event log:

| Signal | Formula | What it measures |
|---|---|---|
| Action efficiency | `N(end) / total_sentences` | Rubik's-cube ratio. Fewer wasted actions = higher |
| Sentence uniqueness | `unique (verb, objectA, objectB) / total_sentences` | Direct measure of repetition |
| Room revisit ratio | `roomEntered_events / unique_rooms` | Back-and-forth. Only penalized when it happens during a zero-novelty streak |
| Plateau | longest stretch where `dN = 0` | Stuck duration |
| Time-to-Nth-novelty | actions/seconds to the 1st, 5th, 10th, 25th novelty event | Discovery speed, chess-Elo style checkpoints |

These are **not** summed into the primary score for v1. They are reported for
diagnosis and later weighting experiments.

### Cross-game aggregation

Per-game scores are reported individually. A **suite score** is the geometric
mean of per-game run-scores, same shape as SPEC benchmarks. Geomean is the
standard choice when per-game scales may differ and no single game should
dominate. The per-game table is shown alongside, so it stays visible which
title contributed what.

## Run fingerprint

Each run record carries enough metadata for cross-run comparability:

- `gameId`, `gameVersion`, `schema`
- `declaredBudget` (one of 5, 10, 30, 60 minutes)
- starting `room`, seed if available
- `stopReason` (explicit / ceiling / crash)
- agent id / version
- mock runs (`snapshot.mock === true`) are excluded from the benchmark pool

## Known farming mitigations

The novelty primitives are designed to resist trivial gaming:

- **Reversible state** (open/close loops): each `(obj, state)` tuple counts once
- **Inventory drop/pickup**: first-acquisition only
- **Dialog replay**: hash the text, each hash once
- **Script re-triggers**: cutscene counted per script-invocation-seq, not per transition
- **Early-quit**: rate computed against declared budget, not elapsed
- **Overtime**: hard ceiling enforces the budget

---

## Fork-side verification report

The harness asked the fork-side agent to confirm or refine the eight novelty
primitives, answer the wasted-action and timebase questions, and propose a
dedicated `__scummBench*` surface. Findings, with `file:line` citations into
this fork:

### Per-primitive verification (universal v3–v6 unless noted)

1. **`room`** — `agent_state.cpp:627` reads `_currentRoom`. Universal.
2. **`roomObjects[]`** — populated from `_objs[]` filtered to
   `OF_OWNER_ROOM` (`agent_state.cpp:386`). Universal; mid-room script
   spawns are caught because the engine itself rewrites `_objs[]`.
3. **`roomObjects[].state`** — read at `agent_state.cpp:403` from
   `od.state`, kept in sync via `putState` (`object.cpp:327`). It is the
   full state byte, but for v3–v5 only the low bits are typically used
   as an "image variant" index. Many puzzle interactions call
   `putState`; some script-internal effects (variable flags only) do
   not surface here.
4. **`inventory[]`** — `agent_state.cpp:423`, filtered by
   `getOwner==egoId`. Universal.
5. **`actors[]`** — `agent_state.cpp:547`. **Filter caveat:** actors
   with `_costume == 0` are dropped (`agent_state.cpp:566`). Some
   games briefly null a costume during scene swaps; NPC discovery is
   otherwise reliable.
6. **`msgText`** — `agent_state.cpp:639` decodes `_charsetBuffer`.
   Covers **all** text routed through the charset system: actor speech,
   narrator, notes, "Look at" inscriptions. ScummVM's own UI overlays
   (save/load) use a different path and are not captured.
7. **`dialogChoices[]`** — `agent_state.cpp:518`. Populated only when
   `haveMsg!=0` and the verb sits above `verbAreaTop`. Reliable on
   v5/v6, untested on v3/v4.
8. **Cutscene** — `inCutscene` from `vm.cutSceneStackPointer`
   (`agent_state.cpp:681`). No event currently fires on transition;
   `kEventObjectStateChanged` (kind 6) and `kEventGameReset` (kind 7)
   are reserved but unused (`agent_state.h:217`). **Add:** an event on
   cutscene begin/end before counting them.

### Q6 — wasted-action detection

Cheapest path is **(b)**: hook the three engine funnels —
`putState` (`object.cpp:327`), `setOwnerOf` (`object.cpp:98`), and
`writeVar` (`script.cpp:713`). If none fire between `doSentence` queueing
and `checkAndRunSentenceScript` returning (`script.cpp:1166`–`1235`), the
sentence was a no-op. There is no canonical "sentence completed" event
yet — see Q9.

### Q7 — `t` semantics

`t` is `g_system->getMillis()` at capture time (`agent_state.cpp:623`) —
wall-clock. Under Emscripten this maps to `Date.now()` and **does not
pause** when the tab is backgrounded; it does, however, advance even when
the engine is paused at the menu. The `seq` field (`agent_state.h:169`) is
a stable monotonic counter incremented per snapshot+event; for rate
denominators it is the safer choice.

### Q9 — proposed `__scummBench*` surface

Recommend a separate publisher fed from existing engine funnels rather
than overloading `__scummPublish`. Highest-value additions, all **cheap**
(single hook in an existing funnel) and **universal** (v3–v6):

- `scriptEntered(scriptId, callerScriptId)` / `scriptExited(scriptId)`
  from `runScript` / `stopScript` (`script.cpp:38`, `script.cpp:262`).
  Single source of truth for script topology.
- `varWritten(var, old, new, scriptId)` from `writeVar`
  (`script.cpp:713`). Catches game-flag updates that puzzles set but
  don't reflect in object `state`.
- `objectStateChanged(obj, old, new)` from `putState` (`object.cpp:327`).
- `ownerChanged(obj, old, new)` from `setOwnerOf` (`object.cpp:98`).
  Catches inventory transfers (give-to-NPC) that the harness-side
  inventory diff currently misses.
- `sentenceResolved(verb, objA, objB, anyEffect)` emitted at the end of
  `checkAndRunSentenceScript` (`script.cpp:1166`). `anyEffect` rolls up
  the put/owner/var hooks above for the cheap wasted-action signal.
- `tickCount` — incremented once per `scummLoop` call (`scumm.cpp:3250`,
  near the existing `_agentRuntime->tick(this)`). Engine-tick monotonic,
  pause-safe.

Skipped: room-graph / exit-topology dumps (medium cost; exits are
script-encoded per room, not a flat static table — not worth the engine
work for v1).

### Q8 — game-specific fields

None of the existing snapshot fields are silently game-specific — the
`gameId`/`gameVersion` columns at the top of every snapshot
(`agent_state.cpp:624`) make any future game-conditional fields
explicit. The two soft caveats are the v3/v4 dialog-choice classifier
(Q7 above) and `roomObjects[].box` precision (already documented in
`AGENT_HARNESS.md` §10).

---

## Next steps (not yet committed)

1. Land the proposed `__scummBench*` hooks behind the existing
   `--enable-agent-telemetry` flag. Schema-version them independently of
   the play-time snapshot schema.
2. Build a minimal recorder on top of the existing `__scummEventsSince`
   stream that maintains the eight monotonic sets and writes a run log.
3. Implement the run-start / run-stop handshake:
   `__benchmarkStart({ game, budgetMinutes, agentId })`,
   `__benchmarkStop(reason)`, plus a human End button in the overlay.
4. Implement the hard-ceiling watchdog.
5. Produce a scoring function that consumes the run log and emits the
   primary score plus the diagnostic efficiency metrics.
6. Build the per-game + suite-level (geomean) leaderboard view.
7. Run a baseline agent + random-action agent on the same game to
   sanity-check that the score separates them.
