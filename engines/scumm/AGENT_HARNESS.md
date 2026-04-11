# Integration notes for `agent-game-harness`

This file is the fork's contract with the
[`agent-game-harness`](https://github.com/rabengraph/agent-game-harness) app
shell. Hand it to whoever is wiring the harness side — it covers everything
the harness needs to know about this fork without reading any C++.

The corresponding implementation lives in:

- `engines/scumm/agent_state.h` — C++ public API (types, Runtime, Collector)
- `engines/scumm/agent_state.cpp` — collector + JSON + runtime + debug sink
- `engines/scumm/agent_bridge_emscripten.cpp` — JS bridge (Emscripten only)
- `engines/scumm/scumm.cpp` — one-line tick from `ScummEngine::scummLoop()`
- `configure` — `--enable-agent-telemetry` flag

---

## 1. Where the fork lives

| Item | Value |
|---|---|
| Fork repo | `git@github.com:rabengraph/scummvm.git` |
| Working branch | **`claude/scummvm-agent-harness-DKVxd`** |

The harness's `scripts/build-scummvm.sh` defaults to `SCUMMVM_AGENT_BRANCH=poc/agent-telemetry`. Override it:

```bash
export SCUMMVM_AGENT_REMOTE=git@github.com:rabengraph/scummvm.git
export SCUMMVM_AGENT_BRANCH=claude/scummvm-agent-harness-DKVxd
./scripts/build-scummvm.sh
```

Or change the defaults in `build-scummvm.sh` directly.

---

## 2. Build flag

The fork recognises a new `./configure` flag:

```
--enable-agent-telemetry   build SCUMM engine with agent telemetry
                           publishing to window.__scummPublish /
                           window.__scummEmit (fork-only feature)
--disable-agent-telemetry  disable (default)
```

The harness's fallback build path already passes it:

```bash
emconfigure ./configure \
    --host=wasm32-unknown-emscripten \
    --enable-debug \
    --disable-all-engines \
    --enable-engine=scumm \
    --enable-agent-telemetry
```

When the flag is on, `config.h` gains `#define ENABLE_SCUMM_AGENT` and the
engine auto-enables telemetry at construction — no ConfMan key or env var
needed. When the flag is off, the engine compiles fine and telemetry stays
dormant (runtime opt-in still works via `ConfMan "agent_telemetry"` or
`SCUMMVM_AGENT_TELEMETRY=1`, useful for local dev on non-agent builds).

---

## 3. JS bridge surface

In an Emscripten build, the engine calls **two hooks installed on `window`**
before the wasm runtime starts. These are the names the harness's
`web/shared/bridge.js` already installs:

```js
window.__scummPublish(snapshotObject)   // full snapshot, ~10 Hz max
window.__scummEmit(eventObject)         // small typed change event
```

Both receive **parsed JavaScript objects**, not JSON strings. The fork
`JSON.parse`s on the JS side of the `EM_ASM` boundary, so the harness can
keep spreading the payload directly (as it already does in `publish()`/
`emit()`).

Lookups are performed on every call — the harness can install, replace, or
temporarily null these hooks at any time. If either hook is missing or is
not a function, the call is a **silent no-op**, so the engine can boot
before the harness page has mounted its bridge.

Errors in the hooks are caught and logged via `console.error`; they never
propagate into the engine.

---

## 4. Snapshot shape

One top-level object per `window.__scummPublish(obj)` call:

```jsonc
{
  "schema": 1,                 // Agent::kSchemaVersion — bump on breaking changes
  "seq": 1234,                 // monotonic counter across snapshots + events
  "t": 71234567,               // g_system->getMillis()
  "gameId": 12,                // Scumm::GameID enum
  "gameVersion": 5,            // Scumm::GameSettings.version
  "gameName": "monkey",        // detection id, for display only

  "room": 10,                  // _currentRoom
  "roomResource": 10,
  "roomWidth": 320,
  "roomHeight": 200,

  "ego": {
    "id": 1,                   // actor id of VAR_EGO (-1 if unknown)
    "room": 10,
    "pos": { "x": 160, "y": 120 },
    "facing": 270,             // engine-native direction
    "walking": false,          // true when any walk flag bit is set
    "costume": 93
  },

  "hover": {
    "objectId": 42,            // 0 if nothing under the cursor
    "objectName": "door",
    "verbId": 3,               // _verbMouseOver
    "mouse": { "x": 160, "y": 120 }   // _mouse (virtual coords)
  },

  "sentence": {
    "verb": 3,                 // top-of-stack SentenceTab
    "preposition": 2,
    "objectA": 42,
    "objectB": 0,
    "active": true             // false when no sentence is queued
  },

  "roomObjects": [
    {
      "id": 42,
      "name": "door",
      "box": { "x": 120, "y": 80, "w": 40, "h": 80 },  // virtual-screen pixels
      "state": 0,
      "owner": 0,              // 0 == room
      "inInventory": false,
      "untouchable": false     // kObjectClassUntouchable
    }
    // ...
  ],

  "inventory": [
    {
      "id": 7,
      "name": "rubber chicken with a pulley in the middle",
      "box": { "x": 0, "y": 0, "w": 0, "h": 0 },  // inventory items have no room box
      "state": 0,
      "owner": 1,              // ego actor id
      "inInventory": true,
      "untouchable": false
    }
    // ...
  ],

  "verbs": [
    {
      "slot": 1,
      "id": 100,
      "name": "Open",
      "box": { "x": 0, "y": 144, "w": 40, "h": 8 },
      "visible": true
    }
    // ...
  ]
}
```

### Field notes

- `roomObjects` only contains objects whose owner is the current room
  (`OF_OWNER_ROOM`). Picked-up items move to `inventory`.
- `inventory` is filtered to items owned by ego (`VAR_EGO`), so you don't
  get other characters' pockets.
- `verbs` lists occupied verb slots only (slot 0 is the sentinel). `name`
  comes from the `rtVerb` resource and can be empty when the resource has
  not been loaded yet; fall back to rendering the numeric `id` in that
  case.
- `box` in the v1 schema is **not** guaranteed to be tight or visually
  perfect — for some games the stored object rect is looser than the
  actual hit region. Good enough for an overlay that catches telemetry
  mistakes; don't rely on it for pixel-perfect click routing.
- `walking` is true when any `MF_*` flag (except `MF_FROZEN`) is set on
  the ego actor.
- `hover.objectId == 0` means the mouse is not over any object. Same goes
  for `hover.verbId`.
- Coordinates are in virtual-screen pixels (the engine's internal
  coordinate space) — not window or canvas pixels. The harness is
  responsible for mapping them to DOM space if it wants to overlay on the
  canvas.

---

## 5. Event shape

Events are small diffs emitted on meaningful state changes, in addition to
(not instead of) the full snapshots. One object per `window.__scummEmit(obj)`
call:

```jsonc
{
  "kind": 1,                   // see table below
  "seq": 1234,                 // shares the counter with snapshots
  "t": 71234567,
  "payload": { /* kind-specific, see below */ }
}
```

### Event kinds

| `kind` | Name                   | Payload |
|:---:|------------------------|---|
| 1 | `roomChanged`          | `{ "from": 9, "to": 10, "resource": 10 }` |
| 2 | `hoverChanged`         | `{ "objectId": 42, "objectName": "door", "verbId": 3 }` |
| 3 | `inventoryChanged`     | `{ "count": 5 }` |
| 4 | `sentenceChanged`      | `{ "verb": 3, "objectA": 42, "objectB": 0, "active": true }` |
| 5 | `egoMoved`             | `{ "room": 10, "x": 160, "y": 120, "walking": false }` |
| 6 | `objectStateChanged`   | *(reserved for future use — not currently emitted by the v1 runtime)* |
| 7 | `gameReset`            | *(reserved for future use — not currently emitted by the v1 runtime)* |

Events 6 and 7 are defined in the schema so clients can be forward-
compatible, but the current runtime does not emit them. `egoMoved` is only
emitted on room change or walk-stop, not on every pixel of motion — the
full snapshot at the capped cadence carries per-frame position updates.

---

## 6. Cadence

- **Snapshots**: rate-limited to **≥100 ms** between emissions (~10 Hz).
  Adjustable via `Agent::Runtime::setSnapshotPeriodMs()`; defaults to 100.
- **Events**: emitted immediately when the diff detects a change, bounded
  only by the frame rate of the SCUMM main loop.
- Both counters share a single `seq` so the harness can order them
  reliably.

---

## 7. Native / dev fallback

When `__EMSCRIPTEN__` is **not** defined (native builds), the Runtime uses a
`DebugLogPublisher` that writes to ScummVM's debug channel:

```
debug(5, "[SCUMM_STATE] {…}");
debug(5, "[SCUMM_EVENT] {…}");
```

Run ScummVM with `--debuglevel=5` to see the stream. Useful for iterating
on the collector without an Emscripten toolchain. Telemetry has to be
opted-in on native builds via `ConfMan "agent_telemetry" = true` or
`SCUMMVM_AGENT_TELEMETRY=1`.

---

## 8. Quick smoke test for the bridge

Once the wasm build is copied into `web/public/scummvm` and the harness
page has loaded its bridge, this should work in the DevTools console on
`/game`:

```js
// after a few seconds of the game running
window.__scummState                // latest snapshot (the harness mirrors it)
window.__scummRead()               // same, via the bridge helper
window.__scummEvents().slice(-5)   // last 5 events
document.getElementById('scumm-state').textContent  // DOM mirror
```

If `window.__scummState` stays null but nothing throws in the console,
either:
1. Telemetry wasn't enabled at build time — re-run
   `./scripts/build-scummvm.sh` with `--enable-agent-telemetry` (it already
   does).
2. The game hasn't loaded a room yet — `Collector::capture()` bails out on
   `_currentRoom == 0 && _numLocalObjects == 0`.
3. `window.__scummPublish` isn't installed before the wasm runtime starts —
   check that `bridge.js` runs before the module constructor on `/game`.

---

## 9. What the harness should *not* rely on yet

- **Object `box` precision.** Good enough for a rough overlay; do not use
  as a hit test.
- **Dialogue choices.** The v1 schema does not yet expose dialog options.
  They will arrive as a new top-level field when added; the harness should
  treat the snapshot as additive.
- **SCUMM version differences.** The collector is engine-generic but has
  only been exercised in the abstract. Behaviour on HE games, v0–v3, and
  v7–v8 may be less useful than on v5–v6 LucasArts adventures.
- **Saves / loading UI.** Out of scope for the fork; handle in the harness.

---

## 10. Schema versioning

Every snapshot carries `schema: 1`. The harness should read this and either:

- accept it as-is (current behaviour), or
- bail out gracefully with a visible message if a future build bumps the
  schema and the harness hasn't caught up.

A schema bump will happen when any field is **removed** or **renamed**.
Adding new fields will not bump the schema; the harness should tolerate
unknown top-level keys.

---

## 11. Source of truth

If anything in this document conflicts with the actual code, the code
wins. The authoritative surfaces are:

- Snapshot / Event layout → `engines/scumm/agent_state.h`
- JSON serialization → `engines/scumm/agent_state.cpp` (`snapshotToJson`,
  `eventToJson`)
- JS hook plumbing → `engines/scumm/agent_bridge_emscripten.cpp`
- Build flag → `configure`, `config.h`
- Engine hook point → `engines/scumm/scumm.cpp` end of
  `ScummEngine::scummLoop()`
