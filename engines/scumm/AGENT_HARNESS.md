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
  "camera": { "x": 0 },        // scroll offset; screen_x = room_x - camera.x
  "haveMsg": 0,                // text state: 0=none, 255=active, 1=ending

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
      "visible": true,
      "kind": 0   // 0=action, 1=inventory, 2=dialog, 3=hidden
    }
    // ...
  ],

  "walkBoxes": [
    {
      "id": 0,
      "ul": { "x": 0, "y": 100 },    // upper-left
      "ur": { "x": 200, "y": 100 },  // upper-right
      "ll": { "x": 0, "y": 150 },    // lower-left
      "lr": { "x": 200, "y": 150 },  // lower-right
      "flags": 0,
      "locked": false,
      "invisible": false
    }
    // ...
  ],

  "inputLocked": false,  // true when _userPut == 0 (player input disabled)
  "inCutscene": false    // true when cutSceneStackPointer > 0
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
- `verbs[].kind` classifies the verb type:
  - `0` = action verb (Open, Close, Pick up, etc.)
  - `1` = inventory slot
  - `2` = dialog choice (when text is active)
  - `3` = hidden (curmode == 0)
- `box` in the v1 schema is **not** guaranteed to be tight or visually
  perfect — for some games the stored object rect is looser than the
  actual hit region. Good enough for an overlay that catches telemetry
  mistakes; don't rely on it for pixel-perfect click routing.
- `walking` is true when any `MF_*` flag (except `MF_FROZEN`) is set on
  the ego actor.
- `camera.x` is the viewport center position. For rooms wider than the
  screen (320 pixels), convert room coordinates to screen coordinates
  with `screen_x = room_x - camera.x + 160` (160 being half of 320).
- `haveMsg` indicates text display state:
  - `0` = no text on screen
  - `255` (0xFF) = text is active, waiting for player to click
  - `1` = text is ending/clearing
  When `haveMsg != 0` and dialog choices are present, the `verbs[]`
  array will contain the dialog options (same slot system as action verbs).
- `hover.objectId == 0` means the mouse is not over any object. Same goes
  for `hover.verbId`.
- `inputLocked` is true when `_userPut == 0`, meaning player input is
  disabled (during animations, text display, etc.). Agents should wait
  for this to become false before sending commands.
- `inCutscene` is true when `cutSceneStackPointer > 0`, indicating a
  cutscene is playing. Commands sent during cutscenes may be ignored.
- `walkBoxes[]` contains the walkable area quadrilaterals. Each has four
  corners (ul, ur, ll, lr) in room coordinates. Use these to understand
  where the ego actor can walk. `locked` walkboxes are temporarily
  impassable; `invisible` walkboxes affect pathfinding but aren't drawn.
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

## 9. Action API (agent → engine)

The harness exposes functions for agents to control the game:

```js
// Click a verb by ID (works for action verbs and dialog choices)
window.__scummClickVerb(verbId)

// Click an object by ID (preferred - bypasses coordinate conversion)
window.__scummClickObject(objectId)

// Execute a complete sentence: verb + object(s)
window.__scummDoSentence({ verb, objectA, objectB })

// Click at room coordinates (use clickObject instead when possible)
window.__scummClickAt(x, y)

// Walk ego to room coordinates
window.__scummWalkTo(x, y)

// Check if action API is ready
window.__scummActionsReady()
```

### Implementation

These functions call into exported C functions in the WASM module:

| JS function | C function | Header |
|-------------|------------|--------|
| `__scummClickVerb(id)` | `agent_click_verb(int)` | `agent_commands.h` |
| `__scummClickObject(id)` | `agent_click_object(int)` | `agent_commands.h` |
| `__scummClickAt(x, y)` | `agent_click_at(int, int)` | `agent_commands.h` |
| `__scummWalkTo(x, y)` | `agent_walk_to(int, int)` | `agent_commands.h` |

The exported functions are marked `EMSCRIPTEN_KEEPALIVE` and accessible as
`Module._agent_*` once the WASM loads. The harness's `bridge.js` wraps them.

### Usage notes

- **Use `clickObject` to interact with objects.** It bypasses coordinate
  space conversions by looking up the object position internally.
- `clickVerb` triggers `runInputScript(kVerbClickArea, verbId, 1)` — the
  same code path as a real mouse click on a verb.
- `doSentence` is a convenience wrapper that clicks the verb, then the
  object(s) with appropriate timing.
- Dialog choices appear in `verbs[]` when `haveMsg != 0`. Click them with
  `__scummClickVerb(verbs[i].id)`.
- Check `inputLocked` and `inCutscene` before sending commands — the engine
  may ignore input during cutscenes or when input is disabled.
- The action API is fire-and-forget; poll `ego.walking`, `haveMsg`, and
  `sentence.active` in the snapshot to know when actions complete.

---

## 10. What the harness should *not* rely on yet (caveats)

- **Object `box` precision.** Good enough for a rough overlay; do not use
  as a hit test.
- **Dialogue choices.** Dialog options appear in `verbs[]` when `haveMsg != 0`.
  They use the same slot system as action verbs. A dedicated `dialogChoices[]`
  field may be added later for clearer separation.
- **SCUMM version differences.** The collector is engine-generic but has
  only been exercised in the abstract. Behaviour on HE games, v0–v3, and
  v7–v8 may be less useful than on v5–v6 LucasArts adventures.
- **Saves / loading UI.** Out of scope for the fork; handle in the harness.

---

## 11. Schema versioning

Every snapshot carries `schema: 1`. The harness should read this and either:

- accept it as-is (current behaviour), or
- bail out gracefully with a visible message if a future build bumps the
  schema and the harness hasn't caught up.

A schema bump will happen when any field is **removed** or **renamed**.
Adding new fields will not bump the schema; the harness should tolerate
unknown top-level keys.

---

## 12. Source of truth

If anything in this document conflicts with the actual code, the code
wins. The authoritative surfaces are:

- Snapshot / Event layout → `engines/scumm/agent_state.h`
- JSON serialization → `engines/scumm/agent_state.cpp` (`snapshotToJson`,
  `eventToJson`)
- JS hook plumbing (outbound) → `engines/scumm/agent_bridge_emscripten.cpp`
- Action API (inbound) → `engines/scumm/agent_commands.{h,cpp}`
- Build flag → `configure`, `config.h`
- Engine hook point → `engines/scumm/scumm.cpp` end of
  `ScummEngine::scummLoop()`
