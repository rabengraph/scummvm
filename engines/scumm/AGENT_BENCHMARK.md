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

### Timebase (was Q7)

`t` is `g_system->getMillis()` at capture time (`agent_state.cpp:623`) —
wall-clock. Under Emscripten this maps to `Date.now()` and **does not
pause** when the tab is backgrounded; it does, however, advance even when
the engine is paused at the menu. The `seq` field (`agent_state.h:169`) is
a stable monotonic counter incremented per snapshot+event; for rate
denominators it is the safer choice.

### Game-specific fields (was Q8)

None of the existing snapshot fields are silently game-specific — the
`gameId`/`gameVersion` columns at the top of every snapshot
(`agent_state.cpp:624`) make any future game-conditional fields
explicit. The two soft caveats are the v3/v4 dialog-choice classifier
and `roomObjects[].box` precision (both documented in
`AGENT_HARNESS.md` §10).

---

## Implementation tradeoff: harness side vs. engine side

**All eight novelty primitives are derivable from the existing playtime
stream** (`__scummPublish` / `__scummEmit`). The harness can compute the
v1 score without any fork-side work beyond what already ships:

| Primitive | Already visible via |
|---|---|
| rooms visited | `snapshot.room` |
| objects seen | `snapshot.roomObjects[]` |
| object-state transitions | diff `roomObjects[].state` across snapshots |
| inventory firsts | `snapshot.inventory[]` |
| actors encountered | `snapshot.actors[]` |
| unique msg lines | `snapshot.msgText` when `haveMsg != 0` |
| dialog branches | `snapshot.dialogChoices[]` |
| cutscenes | `snapshot.inCutscene` false→true |

The diagnostic metrics (total sentences, sentence uniqueness, plateaus,
time-to-Nth-novelty) are similarly derivable: the harness already
receives `sentenceChanged`, `roomChanged`, `inventoryChanged`, and
`egoMoved` events.

The one diagnostic metric that *looks* like it wants engine-internal
data is **wasted-action detection** (Rubik's-cube ratio). Even that is
approximable harness-side: watch for any state diff in the N snapshots
following a `sentenceChanged(active=true)` event. ~100 ms resolution,
good enough for a score computed over minutes.

### Current engine-side status

A `__scummBenchEmit` channel was landed during design exploration (commit
[`4e01d6ae`](../..//commit/4e01d6ae), `agent_bench.{h,cpp}`,
`agent_bench_emscripten.cpp`, `AGENT_HARNESS.md` §13). It exposes
`scriptEntered`, `scriptExited`, `varWritten`, `objectStateChanged`,
`ownerChanged`, `sentenceResolved`, and `tick` events. Known gaps in the
current implementation before it could be relied on:

- Hooks only `runScript`, not `runObjectScript` — misses the main
  verb-on-object dispatch path.
- Hooks only `stopScript`, not `stopObjectCode` / `stopObjectScript` —
  most normal script exits are invisible.
- The sentence payload is read from a reference after the sentence
  script runs; if that script pushes another sentence, the payload is
  stale.
- Only the scumm-vars path of `writeVar` is hooked; v4+ bit variables
  (heavily used for puzzle state) are missed.
- No game-load / savestate signal.

**Decision pending on the harness side:** if the v1 scoring function
only needs the eight novelty primitives above, the engine-side channel
is unnecessary — we should revert `4e01d6ae` rather than maintain a
dark channel with gaps. If the harness has evidence that scoring needs
engine-internal signals, we close the gaps and keep the channel.

**Fork-side recommendation:** start harness-only. Come back to engine-
side telemetry only when a concrete scoring feature demands it.

---

## Additional signals the fork could expose if the harness decides it wants them

Beyond the `__scummBenchEmit` channel already landed, these are the
fork-internal signals the harness might find useful for richer scoring
or post-run analysis. Each entry gives rough cost and whether it is
universal across v3–v6.

### Tier A — genuinely engine-internal (not derivable harness-side)

| Signal | Cost | Universal | Notes |
|---|---|---|---|
| Variable writes (scumm + bit vars) | cheap | yes | Puzzle flags that never surface to object `state`. Required for high-fidelity wasted-action detection. |
| Script enter/exit (incl. object scripts) | cheap | yes | Script topology — which scripts ran, when, nested under which parent. Lets a scoring fn weight "reached a new script" as progress. |
| Sentence rejection reason | cheap | yes | Distinguishes "agent tried an invalid combo" from "valid combo with no effect". Source: `getVerbEntrypoint`, `checkExecVerbs` path. |
| Pause-safe engine-tick | cheap | yes | Monotonic, does not advance while engine is paused. Current `snapshot.t` is wall-clock and advances through pauses. |
| Save-point fires | cheap | yes | Many SCUMM games call `VAR_AUTOSAVE` or trigger built-in save flows at checkpoints. Strong progress signal. |
| Sound/music cue changes | cheap | yes | Music track transitions often mark plot beats. `Sound::playSound` hook. |

### Tier B — derivable harness-side, but lossy or delayed

| Signal | Cost | Universal | Notes |
|---|---|---|---|
| Per-tick object-state events | cheap | yes | Harness can already diff at ~10 Hz via snapshots. Engine-side is per-frame and loss-free. |
| Per-tick owner-change events | cheap | yes | Same. Useful for give-to-NPC detection (currently visible only as inventory size delta). |
| Cutscene begin/end events | cheap | yes | Harness can derive from `inCutscene` diff. Event form removes ambiguity at the boundary. |

### Tier C — static dumps the harness cannot reconstruct

| Signal | Cost | Universal | Notes |
|---|---|---|---|
| Initial state snapshot | cheap | yes | At game start, dump `_objectStateTable` + `_objectOwnerTable`. Lets the scorer compute "fraction of world objects that have ever moved" — a game-agnostic progress estimator. |
| Per-object class flags | cheap | yes | `kObjectClassPickupable`, `kObjectClassUntouchable`, `kObjectClassPlayer`, etc. Lets the harness tell interactable objects from scenery without guessing. |
| Verb-entrypoint matrix per room | medium | yes | For each (object, verb) pair, does the object have a verb script? Enables "coverage" scoring (fraction of valid interactions attempted). |
| Room exit graph | medium | yes | Per room, which objects carry `kObjectClassExit`. Lets the harness build a static map and measure exploration coverage. |

### Tier D — anti-farming and research signals

| Signal | Cost | Universal | Notes |
|---|---|---|---|
| PRNG seed + state | cheap | yes | Detect replay attacks: two runs with identical input and identical seed produce identical event streams. |
| Script hitcount per room | cheap | yes | After N runs of the same script, genuine novelty is zero. Lets the scorer down-weight hit-repetition. |
| Walkbox-visited set | cheap | yes | Fine-grained "did the agent actually cover the room geometry" vs. just entering it. |
| Engine-pause state | cheap | yes | Distinguish "agent thinking" from "game paused by user". Useful as a quality gate on runs. |

### Recommendation on tiers

If v1 shows that harness-only scoring suffices, skip all of the above.
If scoring needs refinement, pull from Tier A and Tier C first — they
add genuinely new information. Tier B is worth adding only for games or
events where 10 Hz snapshot resolution proves insufficient (unlikely
for SCUMM pacing). Tier D is useful once there are multiple agents
competing and replay attacks become a concern.

---

## Next steps

Decision point for the harness side:

1. **Prototype the recorder harness-only** on top of the existing
   `__scummPublish` / `__scummEmit` stream. Maintain the eight monotonic
   sets, produce a run log.
2. **Implement the run-start / run-stop handshake** (harness side):
   `__benchmarkStart({ game, budgetMinutes, agentId })`,
   `__benchmarkStop(reason)`, human End button in the overlay.
3. **Implement the hard-ceiling watchdog** (harness side).
4. **Produce the scoring function** — primary score + diagnostic
   efficiency metrics — from the run log.
5. **Sanity-check**: baseline agent + random-action agent on the same
   game. Scores should separate them.
6. **Decide on engine-side telemetry.** If (4) needs a signal the
   playtime stream can't give, pick from the tiers above and ask the
   fork side to wire it. Otherwise, revert commit `4e01d6ae` so the
   fork doesn't carry a dead channel.
7. **Per-game and suite-level leaderboard** (geomean across games).
