/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef SCUMM_AGENT_STATE_H
#define SCUMM_AGENT_STATE_H

// --- ScummVM Agent Telemetry ----------------------------------------------
//
// Fork-native telemetry surface for the agent-game-harness browser app.
// The harness's `web/shared/bridge.js` installs two hooks on `window`
// before loading the wasm runtime, and the Emscripten build of this
// module publishes to them:
//
//     window.__scummPublish(snapshotObject)   // full snapshot (rate-limited)
//     window.__scummEmit(eventObject)         // small typed change event
//
// Both hooks receive parsed JavaScript objects. JSON is produced on the
// C++ side (here) and parsed on the JS side by the bridge code in
// agent_bridge_emscripten.cpp. If the hooks are missing, the calls are
// silent no-ops — so the engine can start before the harness mounts.
//
// Public C++ surface
// ------------------
//  * Scumm::Agent::Snapshot   - POD description of the current game state
//  * Scumm::Agent::Event      - small typed change event
//  * Scumm::Agent::Publisher  - abstract output sink
//  * Scumm::Agent::Runtime    - per-engine runtime that diffs + rate-limits
//  * Scumm::Agent::Collector  - friend of ScummEngine; captures snapshots
//
// Build-target plumbing
// ---------------------
// `createDefaultPublisher()` picks an appropriate sink at compile time:
//   - Emscripten / web build  -> window.__scummPublish / __scummEmit
//   - everything else         -> debug-log publisher
//
// The web publisher lives in agent_bridge_emscripten.cpp and is only
// active when __EMSCRIPTEN__ is defined.
//
// Build-time and runtime gating
// -----------------------------
// When ScummVM's configure script is invoked with `--enable-agent-telemetry`,
// it defines ENABLE_SCUMM_AGENT and telemetry auto-enables at engine start.
// Otherwise, telemetry stays off unless opted in via either:
//   - ConfMan key "agent_telemetry" = "true"
//   - environment variable SCUMMVM_AGENT_TELEMETRY=1
// --------------------------------------------------------------------------

#include "common/scummsys.h"
#include "common/array.h"
#include "common/str.h"

namespace Scumm {

class ScummEngine;

namespace Agent {

/// Our own telemetry schema version. Bumped when fields change.
static const int kSchemaVersion = 1;

struct Vec2 {
	int16 x;
	int16 y;
	Vec2() : x(0), y(0) {}
	Vec2(int16 ax, int16 ay) : x(ax), y(ay) {}
};

struct Rect {
	int16 x;
	int16 y;
	int16 w;
	int16 h;
	Rect() : x(0), y(0), w(0), h(0) {}
};

struct EgoInfo {
	int id;              ///< Actor id of VAR_EGO, or -1 if unknown.
	int room;            ///< Room the ego currently resides in.
	Vec2 pos;            ///< Virtual-screen pixel position.
	int facing;          ///< Direction, engine-native (0..359 or 0..3).
	bool walking;        ///< True if any walk flag is currently set.
	int costume;
	EgoInfo() : id(-1), room(0), facing(0), walking(false), costume(0) {}
};

struct ObjectInfo {
	int id;              ///< SCUMM object number.
	Common::String name; ///< Human-readable name, empty if unavailable.
	Rect box;            ///< Bounding box in virtual-screen coordinates.
	int state;           ///< SCUMM object state byte.
	int owner;           ///< Owner actor id; 0 means the current room.
	bool inInventory;    ///< True if this object is in somebody's inventory.
	bool untouchable;    ///< Matches kObjectClassUntouchable / V2 state.
	ObjectInfo() : id(0), state(0), owner(0), inInventory(false), untouchable(false) {}
};

struct VerbInfo {
	int slot;
	int id;
	Common::String name; ///< Verb display string, empty if not retrievable.
	Rect box;
	bool visible;
	int8 kind;           ///< 0=action, 1=inventory, 2=dialog, 3=hidden
	VerbInfo() : slot(0), id(0), visible(false), kind(0) {}
};

struct HoverInfo {
	int objectId;                 ///< Currently hovered object (0 if none).
	Common::String objectName;
	int verbId;                   ///< _verbMouseOver.
	Vec2 mouse;                   ///< Virtual mouse coordinates.
	HoverInfo() : objectId(0), verbId(0) {}
};

struct SentenceInfo {
	int verb;            ///< Verb byte of the top-of-stack sentence.
	int preposition;
	int objectA;
	int objectB;
	bool active;         ///< True when a sentence is currently composed.
	SentenceInfo() : verb(0), preposition(0), objectA(0), objectB(0), active(false) {}
};

struct CameraInfo {
	int16 x;                 ///< Camera X offset (scroll position).
	CameraInfo() : x(0) {}
};

struct WalkboxInfo {
	int8 id;             ///< Walkbox index.
	Vec2 ul, ur, ll, lr; ///< Four corners (upper-left, upper-right, lower-left, lower-right).
	byte flags;          ///< Raw flags byte.
	bool locked;         ///< kBoxLocked flag.
	bool invisible;      ///< kBoxInvisible flag.
	WalkboxInfo() : id(0), flags(0), locked(false), invisible(false) {}
};

struct Snapshot {
	int schemaVersion;       ///< Agent::kSchemaVersion
	uint32 seq;              ///< Monotonic counter, one per emit.
	uint32 timestampMs;      ///< g_system->getMillis() at capture time.

	int gameId;              ///< Scumm::GameID
	int gameVersion;         ///< Scumm::GameSettings.version
	Common::String gameName; ///< Short identifier; for display only.

	int room;                ///< _currentRoom
	int roomResource;        ///< _roomResource
	int roomWidth;
	int roomHeight;
	CameraInfo camera;       ///< Camera/viewport offset.
	int haveMsg;             ///< Text display state: 0=none, 0xFF=active, 1=ending.

	EgoInfo ego;
	HoverInfo hover;
	SentenceInfo sentence;

	Common::Array<ObjectInfo> roomObjects;
	Common::Array<ObjectInfo> inventory;
	Common::Array<VerbInfo> verbs;
	Common::Array<WalkboxInfo> walkBoxes;

	bool inputLocked;        ///< True when _userPut == 0 (input disabled).
	bool inCutscene;         ///< True when cutSceneStackPointer > 0.

	Snapshot() :
		schemaVersion(kSchemaVersion),
		seq(0),
		timestampMs(0),
		gameId(0),
		gameVersion(0),
		room(0),
		roomResource(0),
		roomWidth(0),
		roomHeight(0),
		haveMsg(0),
		inputLocked(false),
		inCutscene(false) {}

	void clear() { *this = Snapshot(); }
};

enum EventKind {
	kEventRoomChanged = 1,
	kEventHoverChanged,
	kEventInventoryChanged,
	kEventSentenceChanged,
	kEventEgoMoved,
	kEventObjectStateChanged,
	kEventGameReset
};

struct Event {
	EventKind kind;
	uint32 seq;
	uint32 timestampMs;
	Common::String payloadJson; ///< Pre-serialized minimal payload.
	Event() : kind(kEventRoomChanged), seq(0), timestampMs(0) {}
};

/**
 * Abstract output sink. Implementations choose how to actually get the
 * data out of the engine process (JS bridge, debug log, file, ...).
 */
class Publisher {
public:
	virtual ~Publisher() {}
	virtual void publishSnapshot(const Common::String &json) = 0;
	virtual void publishEvent(const Common::String &json) = 0;
};

Publisher *createDefaultPublisher();
Publisher *createDebugLogPublisher();

#ifdef __EMSCRIPTEN__
Publisher *createEmscriptenPublisher();
#endif

/// JSON helpers (pure functions, no engine access).
Common::String snapshotToJson(const Snapshot &snap);
Common::String eventToJson(const Event &ev);

/**
 * Captures Snapshot values out of a running ScummEngine.
 *
 * This class is declared as a friend of ScummEngine in scumm.h so it can
 * read the protected engine members it needs. It contains *only* static
 * members and is stateless.
 */
class Collector {
public:
	/// Returns false if the engine is not currently sampleable
	/// (e.g. pre-boot, no loaded room).
	static bool capture(ScummEngine *engine, Snapshot &out);

private:
	Collector();

	static void fillEgo(ScummEngine *engine, Snapshot &out);
	static void fillRoomObjects(ScummEngine *engine, Snapshot &out);
	static void fillInventory(ScummEngine *engine, Snapshot &out);
	static void fillVerbs(ScummEngine *engine, Snapshot &out);
	static void fillHover(ScummEngine *engine, Snapshot &out);
	static void fillSentence(ScummEngine *engine, Snapshot &out);
	static void fillWalkboxes(ScummEngine *engine, Snapshot &out);

	static Common::String safeObjectName(ScummEngine *engine, int obj);
};

/**
 * Per-engine telemetry runtime.
 *
 * Owns a Publisher, rate-limits snapshot emission, compares against the
 * last emitted snapshot to derive minimal events, and exposes
 * `lastSnapshot()` so the harness-side state panel can read the last
 * value synchronously without waiting for the next publish.
 *
 * Cheap when disabled: `tick()` short-circuits on `!_enabled`.
 */
class Runtime {
public:
	Runtime();
	~Runtime();

	/// Called once per scumm frame. Safe to call every frame.
	void tick(ScummEngine *engine);

	/// Drop diff state (use on game reset / load).
	void reset();

	/// Replace the publisher; takes ownership of `p`.
	void setPublisher(Publisher *p);

	/// Returns the most recent snapshot we computed (may be empty).
	const Snapshot &lastSnapshot() const { return _last; }

	bool enabled() const { return _enabled; }
	void setEnabled(bool on) { _enabled = on; }

	/// Min period between full snapshot emissions, in ms.
	void setSnapshotPeriodMs(uint32 ms) { _minPeriodMs = ms; }

private:
	void detectAndEmitEvents(const Snapshot &prev, const Snapshot &next);
	void emitEvent(EventKind kind, const Common::String &payloadJson,
	               uint32 nowMs);

	Publisher *_publisher;
	Snapshot _last;
	bool _hasLast;
	uint32 _seq;
	uint32 _lastSnapshotMs;
	uint32 _minPeriodMs;
	bool _enabled;
};

/// Small helper used by Runtime and by ScummEngine setup code to decide
/// whether telemetry should be turned on at all.
bool telemetryEnabledByConfig();

} // namespace Agent
} // namespace Scumm

#endif
