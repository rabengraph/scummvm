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

// We call getenv() from telemetryEnabledByConfig() so the harness can
// flip telemetry on without touching the ScummVM config file. Must be
// defined before any ScummVM header is included.
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "scumm/agent_state.h"

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/str.h"
#include "common/system.h"

#include "scumm/scumm.h"
#include "scumm/actor.h"
#include "scumm/boxes.h"
#include "scumm/gfx.h"
#include "scumm/object.h"
#include "scumm/script.h"
#include "scumm/verbs.h"

#include <stdlib.h> // getenv

namespace Scumm {
namespace Agent {

// --------------------------------------------------------------------------
// JSON helpers (hand-rolled; no external dep).
// --------------------------------------------------------------------------

static void appendEscaped(Common::String &out, const Common::String &s) {
	out += '"';
	for (uint i = 0; i < s.size(); ++i) {
		char c = s[i];
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if ((unsigned char)c < 0x20) {
				out += Common::String::format("\\u%04x", (unsigned char)c);
			} else {
				out += c;
			}
		}
	}
	out += '"';
}

static void kvInt(Common::String &out, const char *key, int value, bool first) {
	if (!first)
		out += ',';
	out += Common::String::format("\"%s\":%d", key, value);
}

static void kvUint(Common::String &out, const char *key, uint32 value, bool first) {
	if (!first)
		out += ',';
	out += Common::String::format("\"%s\":%u", key, value);
}

static void kvBool(Common::String &out, const char *key, bool value, bool first) {
	if (!first)
		out += ',';
	out += Common::String::format("\"%s\":%s", key, value ? "true" : "false");
}

static void kvString(Common::String &out, const char *key,
                     const Common::String &value, bool first) {
	if (!first)
		out += ',';
	out += Common::String::format("\"%s\":", key);
	appendEscaped(out, value);
}

static void writeRect(Common::String &out, const Rect &r) {
	out += Common::String::format("{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
	                              r.x, r.y, r.w, r.h);
}

static void writeVec2(Common::String &out, const Vec2 &v) {
	out += Common::String::format("{\"x\":%d,\"y\":%d}", v.x, v.y);
}

static void writeObject(Common::String &out, const ObjectInfo &o) {
	out += '{';
	kvInt(out, "id", o.id, true);
	kvString(out, "name", o.name, false);
	out += ",\"box\":";
	writeRect(out, o.box);
	kvInt(out, "state", o.state, false);
	kvInt(out, "owner", o.owner, false);
	kvBool(out, "inInventory", o.inInventory, false);
	kvBool(out, "untouchable", o.untouchable, false);
	out += '}';
}

static void writeVerb(Common::String &out, const VerbInfo &v) {
	out += '{';
	kvInt(out, "slot", v.slot, true);
	kvInt(out, "id", v.id, false);
	kvString(out, "name", v.name, false);
	out += ",\"box\":";
	writeRect(out, v.box);
	kvBool(out, "visible", v.visible, false);
	kvInt(out, "kind", v.kind, false);
	out += '}';
}

static void writeActor(Common::String &out, const ActorInfo &a) {
	out += '{';
	kvInt(out, "id", a.id, true);
	kvString(out, "name", a.name, false);
	kvInt(out, "room", a.room, false);
	out += ",\"pos\":";
	writeVec2(out, a.pos);
	kvInt(out, "facing", a.facing, false);
	kvBool(out, "walking", a.walking, false);
	kvInt(out, "costume", a.costume, false);
	out += '}';
}

static void writeWalkbox(Common::String &out, const WalkboxInfo &wb) {
	out += '{';
	kvInt(out, "id", wb.id, true);
	out += ",\"ul\":";
	writeVec2(out, wb.ul);
	out += ",\"ur\":";
	writeVec2(out, wb.ur);
	out += ",\"ll\":";
	writeVec2(out, wb.ll);
	out += ",\"lr\":";
	writeVec2(out, wb.lr);
	kvInt(out, "flags", wb.flags, false);
	kvBool(out, "locked", wb.locked, false);
	kvBool(out, "invisible", wb.invisible, false);
	out += '}';
}

Common::String snapshotToJson(const Snapshot &s) {
	Common::String out;
	out += '{';
	kvInt(out, "schema", s.schemaVersion, true);
	kvUint(out, "seq", s.seq, false);
	kvUint(out, "t", s.timestampMs, false);
	kvInt(out, "gameId", s.gameId, false);
	kvInt(out, "gameVersion", s.gameVersion, false);
	kvString(out, "gameName", s.gameName, false);
	kvInt(out, "room", s.room, false);
	kvInt(out, "roomResource", s.roomResource, false);
	kvInt(out, "roomWidth", s.roomWidth, false);
	kvInt(out, "roomHeight", s.roomHeight, false);
	out += ",\"camera\":{";
	kvInt(out, "x", s.camera.x, true);
	out += '}';
	kvInt(out, "haveMsg", s.haveMsg, false);
	if (!s.msgText.empty()) {
		kvString(out, "msgText", s.msgText, false);
	}
	kvInt(out, "talkingActor", s.talkingActor, false);

	// ego
	out += ",\"ego\":{";
	kvInt(out, "id", s.ego.id, true);
	kvInt(out, "room", s.ego.room, false);
	out += ",\"pos\":";
	writeVec2(out, s.ego.pos);
	kvInt(out, "facing", s.ego.facing, false);
	kvBool(out, "walking", s.ego.walking, false);
	kvInt(out, "costume", s.ego.costume, false);
	out += '}';

	// hover
	out += ",\"hover\":{";
	kvInt(out, "objectId", s.hover.objectId, true);
	kvString(out, "objectName", s.hover.objectName, false);
	kvInt(out, "verbId", s.hover.verbId, false);
	out += ",\"mouse\":";
	writeVec2(out, s.hover.mouse);
	out += '}';

	// sentence
	out += ",\"sentence\":{";
	kvInt(out, "verb", s.sentence.verb, true);
	kvInt(out, "preposition", s.sentence.preposition, false);
	kvInt(out, "objectA", s.sentence.objectA, false);
	kvInt(out, "objectB", s.sentence.objectB, false);
	kvBool(out, "active", s.sentence.active, false);
	out += '}';

	// room objects
	out += ",\"roomObjects\":[";
	for (uint i = 0; i < s.roomObjects.size(); ++i) {
		if (i)
			out += ',';
		writeObject(out, s.roomObjects[i]);
	}
	out += ']';

	// inventory
	out += ",\"inventory\":[";
	for (uint i = 0; i < s.inventory.size(); ++i) {
		if (i)
			out += ',';
		writeObject(out, s.inventory[i]);
	}
	out += ']';

	// actors (in current room, excluding ego)
	out += ",\"actors\":[";
	for (uint i = 0; i < s.actors.size(); ++i) {
		if (i)
			out += ',';
		writeActor(out, s.actors[i]);
	}
	out += ']';

	// verbs
	out += ",\"verbs\":[";
	for (uint i = 0; i < s.verbs.size(); ++i) {
		if (i)
			out += ',';
		writeVerb(out, s.verbs[i]);
	}
	out += ']';

	// dialog choices (convenience subset of verbs with kind==2)
	out += ",\"dialogChoices\":[";
	for (uint i = 0; i < s.dialogChoices.size(); ++i) {
		if (i)
			out += ',';
		writeVerb(out, s.dialogChoices[i]);
	}
	out += ']';

	// walkboxes
	out += ",\"walkBoxes\":[";
	for (uint i = 0; i < s.walkBoxes.size(); ++i) {
		if (i)
			out += ',';
		writeWalkbox(out, s.walkBoxes[i]);
	}
	out += ']';

	// input/cutscene state
	kvBool(out, "inputLocked", s.inputLocked, false);
	kvBool(out, "inCutscene", s.inCutscene, false);

	out += '}';
	return out;
}

Common::String eventToJson(const Event &ev) {
	Common::String out;
	out += '{';
	kvInt(out, "kind", (int)ev.kind, true);
	kvUint(out, "seq", ev.seq, false);
	kvUint(out, "t", ev.timestampMs, false);
	out += ",\"payload\":";
	if (ev.payloadJson.empty())
		out += "null";
	else
		out += ev.payloadJson;
	out += '}';
	return out;
}

// --------------------------------------------------------------------------
// Publishers
// --------------------------------------------------------------------------

namespace {

class DebugLogPublisher : public Publisher {
public:
	virtual void publishSnapshot(const Common::String &json) override {
		// Tag intentionally mirrors the harness' preferred console convention,
		// but treat it as an internal default — the bridge layer may rewrite it.
		debug(5, "[SCUMM_STATE] %s", json.c_str());
	}
	virtual void publishEvent(const Common::String &json) override {
		debug(5, "[SCUMM_EVENT] %s", json.c_str());
	}
};

} // anonymous namespace

Publisher *createDebugLogPublisher() {
	return new DebugLogPublisher();
}

Publisher *createDefaultPublisher() {
#ifdef __EMSCRIPTEN__
	return createEmscriptenPublisher();
#else
	return createDebugLogPublisher();
#endif
}

// --------------------------------------------------------------------------
// Runtime gating helper
// --------------------------------------------------------------------------

bool telemetryEnabledByConfig() {
	if (ConfMan.hasKey("agent_telemetry") &&
	    ConfMan.getBool("agent_telemetry")) {
		return true;
	}
	const char *env = getenv("SCUMMVM_AGENT_TELEMETRY");
	if (env && env[0] && env[0] != '0')
		return true;
	return false;
}

// --------------------------------------------------------------------------
// Collector
// --------------------------------------------------------------------------

Common::String Collector::safeObjectName(ScummEngine *engine, int obj) {
	if (!engine || obj <= 0)
		return Common::String();
	const byte *n = engine->getObjOrActorName(obj);
	if (!n)
		return Common::String();
	return Common::String((const char *)n);
}

void Collector::fillEgo(ScummEngine *engine, Snapshot &out) {
	out.ego = EgoInfo();
	if (engine->VAR_EGO == 0xFF)
		return;
	int egoId = engine->VAR(engine->VAR_EGO);
	out.ego.id = egoId;
	if (egoId <= 0 || egoId >= engine->_numActors || !engine->_actors)
		return;
	Actor *a = engine->_actors[egoId];
	if (!a)
		return;
	Common::Point p = a->getRealPos();
	out.ego.pos = Vec2((int16)p.x, (int16)p.y);
	out.ego.facing = a->getFacing();
	out.ego.room = a->getRoom();
	out.ego.costume = (int)a->_costume;
	// _moving is a bitmask; treat any walk-related flag as "walking".
	out.ego.walking = (a->_moving & ~MF_FROZEN) != 0;
}

void Collector::fillRoomObjects(ScummEngine *engine, Snapshot &out) {
	out.roomObjects.clear();
	if (!engine->_objs || engine->_numLocalObjects <= 0)
		return;
	out.roomObjects.reserve(engine->_numLocalObjects);
	for (int i = 1; i < engine->_numLocalObjects; ++i) {
		const ObjectData &od = engine->_objs[i];
		if (od.obj_nr == 0)
			continue;

		ObjectInfo info;
		info.id = (int)od.obj_nr;
		info.name = safeObjectName(engine, info.id);
		info.box.x = od.x_pos;
		info.box.y = od.y_pos;
		info.box.w = (int16)od.width;
		info.box.h = (int16)od.height;
		info.state = (int)od.state;
		info.inInventory = false;

		// Only room objects here — skip owned items.
		if (engine->_objectOwnerTable &&
		    info.id >= 0 &&
		    info.id < engine->_numGlobalObjects &&
		    engine->_objectOwnerTable[info.id] != engine->OF_OWNER_ROOM) {
			continue;
		}
		info.owner = (engine->_objectOwnerTable &&
		              info.id >= 0 &&
		              info.id < engine->_numGlobalObjects)
		             ? engine->_objectOwnerTable[info.id] : 0;
		info.untouchable = engine->getClass(info.id, kObjectClassUntouchable);

		out.roomObjects.push_back(info);
	}
}

void Collector::fillInventory(ScummEngine *engine, Snapshot &out) {
	out.inventory.clear();
	if (!engine->_inventory || engine->_numInventory <= 0)
		return;
	int egoId = (engine->VAR_EGO != 0xFF) ? engine->VAR(engine->VAR_EGO) : 0;
	for (int i = 0; i < engine->_numInventory; ++i) {
		uint16 obj = engine->_inventory[i];
		if (obj == 0)
			continue;
		// Only show items owned by ego, not general inventory slot junk.
		if (egoId > 0 && engine->getOwner((int)obj) != egoId)
			continue;

		ObjectInfo info;
		info.id = (int)obj;
		info.name = safeObjectName(engine, info.id);
		info.state = engine->getState(info.id);
		info.owner = engine->getOwner(info.id);
		info.inInventory = true;
		out.inventory.push_back(info);
	}
}

void Collector::fillVerbs(ScummEngine *engine, Snapshot &out) {
	out.verbs.clear();
	out.dialogChoices.clear();
	if (!engine->_verbs || engine->_numVerbs <= 0)
		return;

	// The verb virtual screen's topline marks where the verb/inventory
	// area starts.  Anything above it is "main screen" territory —
	// visible verbs placed there are dialog choices, not action verbs.
	const int verbAreaTop = engine->_virtscr[kVerbVirtScreen].topline;

	// Verb slot 0 is the sentinel / not-in-use slot.
	for (int i = 1; i < engine->_numVerbs; ++i) {
		const VerbSlot &v = engine->_verbs[i];
		if (v.verbid == 0)
			continue;

		VerbInfo vi;
		vi.slot = i;
		vi.id = v.verbid;
		vi.visible = (v.curmode != 0);
		vi.box.x = v.curRect.left;
		vi.box.y = v.curRect.top;
		vi.box.w = (int16)(v.curRect.right - v.curRect.left);
		vi.box.h = (int16)(v.curRect.bottom - v.curRect.top);

		// Classify verb kind:
		// 0 = action (normal verb like Open, Close, etc.)
		// 1 = inventory slot
		// 2 = dialog choice (positioned above verb bar area)
		// 3 = hidden (curmode == 0)
		if (v.curmode == 0) {
			vi.kind = 3; // hidden
		} else if (v.saveid != 0) {
			vi.kind = 1; // inventory slot
		} else if (v.curRect.top < verbAreaTop && v.curRect.top > 0) {
			// Dialog choices are visible, non-inventory verbs positioned
			// above the verb area (verbAreaTop, typically 144 in classic
			// SCUMM v5).  We also require top > 0 to exclude verbs that
			// haven't been positioned yet (curRect is zero-initialized).
			vi.kind = 2; // dialog choice
		} else {
			vi.kind = 0; // action verb
		}

		// Skip truly useless entries (no name and hidden)
		if (vi.kind == 3) {
			// Still include hidden verbs so agent knows what exists
		}

		// Verb display string is stored in rtVerb resources. We need
		// to decode it through convertMessageToString() (the same path
		// drawVerb uses) to handle SCUMM control codes and 0xFF prefixes.
		// Image-type verbs (kImageVerbType) have no text — skip them.
		if (v.type == kTextVerbType) {
			const byte *raw = engine->getResourceAddress(rtVerb, i);
			if (raw) {
				byte decoded[270];
				memset(decoded, 0, sizeof(decoded));
				engine->convertMessageToString(raw, decoded, sizeof(decoded));

				// Skip any leading 0xFF control sequences (4 bytes each).
				const byte *msg = decoded;
				while (*msg == 0xFF)
					msg += 4;

				vi.name = Common::String((const char *)msg);
			}
		}
		out.verbs.push_back(vi);

		// Collect dialog choices into their own array for easy agent access.
		if (vi.kind == 2 && vi.visible && !vi.name.empty()) {
			out.dialogChoices.push_back(vi);
		}
	}
}

void Collector::fillHover(ScummEngine *engine, Snapshot &out) {
	out.hover = HoverInfo();
	out.hover.mouse = Vec2((int16)engine->_mouse.x, (int16)engine->_mouse.y);
	out.hover.verbId = (int)engine->_verbMouseOver;
	int obj = engine->findObject(engine->_mouse.x, engine->_mouse.y);
	if (obj > 0) {
		out.hover.objectId = obj;
		out.hover.objectName = safeObjectName(engine, obj);
	}
}

void Collector::fillSentence(ScummEngine *engine, Snapshot &out) {
	out.sentence = SentenceInfo();
	if (engine->_sentenceNum > 0) {
		const SentenceTab &st = engine->_sentence[engine->_sentenceNum - 1];
		out.sentence.verb = (int)st.verb;
		out.sentence.preposition = (int)st.preposition;
		out.sentence.objectA = (int)st.objectA;
		out.sentence.objectB = (int)st.objectB;
		out.sentence.active = true;
	}
}

void Collector::fillActors(ScummEngine *engine, Snapshot &out) {
	out.actors.clear();
	if (!engine->_actors || engine->_numActors <= 0)
		return;

	int egoId = (engine->VAR_EGO != 0xFF) ? engine->VAR(engine->VAR_EGO) : -1;

	// Actor 0 is a sentinel in SCUMM; start at 1.
	for (int i = 1; i < engine->_numActors; ++i) {
		Actor *a = engine->_actors[i];
		if (!a)
			continue;
		// Skip ego — already reported in out.ego.
		if (i == egoId)
			continue;
		// Only include actors in the current room.
		if (a->getRoom() != engine->_currentRoom)
			continue;
		// Skip actors with no costume (not rendered / inactive).
		if (a->_costume == 0)
			continue;

		ActorInfo ai;
		ai.id = i;
		Common::Point p = a->getRealPos();
		ai.pos = Vec2((int16)p.x, (int16)p.y);
		ai.room = a->getRoom();
		ai.facing = a->getFacing();
		ai.walking = (a->_moving & ~MF_FROZEN) != 0;
		ai.costume = (int)a->_costume;

		// Actor names: getActorName() is available on Actor instances.
		const byte *n = a->getActorName();
		if (n && n[0])
			ai.name = Common::String((const char *)n);

		out.actors.push_back(ai);
	}
}

void Collector::fillWalkboxes(ScummEngine *engine, Snapshot &out) {
	out.walkBoxes.clear();
	byte numBoxes = engine->getNumBoxes();
	if (numBoxes == 0)
		return;

	out.walkBoxes.reserve(numBoxes);
	for (byte i = 0; i < numBoxes; ++i) {
		BoxCoords coords = engine->getBoxCoordinates(i);
		byte flags = engine->getBoxFlags(i);

		WalkboxInfo wi;
		wi.id = i;
		wi.ul = Vec2((int16)coords.ul.x, (int16)coords.ul.y);
		wi.ur = Vec2((int16)coords.ur.x, (int16)coords.ur.y);
		wi.ll = Vec2((int16)coords.ll.x, (int16)coords.ll.y);
		wi.lr = Vec2((int16)coords.lr.x, (int16)coords.lr.y);
		wi.flags = flags;
		wi.locked = (flags & kBoxLocked) != 0;
		wi.invisible = (flags & kBoxInvisible) != 0;

		out.walkBoxes.push_back(wi);
	}
}

bool Collector::capture(ScummEngine *engine, Snapshot &out) {
	if (!engine)
		return false;

	out.clear();

	// Pre-boot and blank-room states are not sampleable yet.
	if (engine->_currentRoom == 0 && engine->_numLocalObjects == 0)
		return false;

	out.schemaVersion = kSchemaVersion;
	out.timestampMs = g_system ? g_system->getMillis() : 0;
	out.gameId = (int)engine->_game.id;
	out.gameVersion = (int)engine->_game.version;
	out.gameName = Common::String(engine->_game.gameid ? engine->_game.gameid : "");
	out.room = (int)engine->_currentRoom;
	out.roomResource = engine->_roomResource;
	out.roomWidth = engine->_roomWidth;
	out.roomHeight = engine->_roomHeight;
	out.camera.x = (int16)engine->camera._cur.x;
	out.haveMsg = (int)engine->_haveMsg;

	// Extract current message text from the charset buffer.
	// _charsetBuffer holds the full message string; _charsetBufPos is
	// how far the engine has rendered so far (letter-by-letter display).
	// We decode it through convertMessageToString() (same as verb text)
	// to handle SCUMM control codes, then strip any remaining artifacts.
	if (engine->_haveMsg != 0 && engine->_charsetBuffer[0] != 0) {
		byte decoded[512];
		memset(decoded, 0, sizeof(decoded));
		engine->convertMessageToString(engine->_charsetBuffer, decoded, sizeof(decoded));

		// Skip leading 0xFF control sequences.
		const byte *msg = decoded;
		while (*msg == 0xFF)
			msg += 4;

		// Final cleanup: strip remaining non-printable chars except
		// newline and space, and normalize ` and ^ artifacts.
		Common::String clean;
		for (const byte *p = msg; *p; ++p) {
			byte c = *p;
			if (c == 0xFF) {
				// Skip inline 0xFF control sequences (4 bytes total).
				p += 3;
				continue;
			}
			if (c >= 0x20 || c == '\n') {
				clean += (char)c;
			}
		}
		out.msgText = clean;
	}

	out.talkingActor = engine->getTalkingActor();

	fillEgo(engine, out);
	fillActors(engine, out);
	fillHover(engine, out);
	fillSentence(engine, out);
	fillRoomObjects(engine, out);
	fillInventory(engine, out);
	fillVerbs(engine, out);
	fillWalkboxes(engine, out);

	// Input state
	// checkExecVerbs() rejects input when _userPut <= 0, not just == 0.
	// Nested cutscenes can push _userPut negative; report that as locked.
	out.inputLocked = (engine->_userPut <= 0);
	out.inCutscene = (engine->vm.cutSceneStackPointer > 0);

	return true;
}

// --------------------------------------------------------------------------
// Runtime
// --------------------------------------------------------------------------

Runtime::Runtime() :
	_publisher(nullptr),
	_hasLast(false),
	_seq(0),
	_lastSnapshotMs(0),
	_minPeriodMs(100), // ~10 Hz cap by default
	_enabled(false) {
}

Runtime::~Runtime() {
	delete _publisher;
}

void Runtime::setPublisher(Publisher *p) {
	if (_publisher == p)
		return;
	delete _publisher;
	_publisher = p;
}

void Runtime::reset() {
	_last.clear();
	_hasLast = false;
}

void Runtime::emitEvent(EventKind kind, const Common::String &payloadJson, uint32 nowMs) {
	if (!_publisher)
		return;
	Event ev;
	ev.kind = kind;
	ev.seq = ++_seq;
	ev.timestampMs = nowMs;
	ev.payloadJson = payloadJson;
	_publisher->publishEvent(eventToJson(ev));
}

void Runtime::detectAndEmitEvents(const Snapshot &prev, const Snapshot &next) {
	const uint32 nowMs = next.timestampMs;

	if (prev.room != next.room) {
		emitEvent(kEventRoomChanged,
		          Common::String::format(
		              "{\"from\":%d,\"to\":%d,\"resource\":%d}",
		              prev.room, next.room, next.roomResource),
		          nowMs);
	}

	if (prev.hover.objectId != next.hover.objectId ||
	    prev.hover.verbId != next.hover.verbId) {
		Common::String p;
		p += '{';
		kvInt(p, "objectId", next.hover.objectId, true);
		kvString(p, "objectName", next.hover.objectName, false);
		kvInt(p, "verbId", next.hover.verbId, false);
		p += '}';
		emitEvent(kEventHoverChanged, p, nowMs);
	}

	if (prev.inventory.size() != next.inventory.size()) {
		emitEvent(kEventInventoryChanged,
		          Common::String::format("{\"count\":%u}",
		                                 (uint)next.inventory.size()),
		          nowMs);
	} else {
		// Also catch same-count swaps.
		for (uint i = 0; i < next.inventory.size(); ++i) {
			if (i >= prev.inventory.size() ||
			    prev.inventory[i].id != next.inventory[i].id) {
				emitEvent(kEventInventoryChanged,
				          Common::String::format("{\"count\":%u}",
				                                 (uint)next.inventory.size()),
				          nowMs);
				break;
			}
		}
	}

	if (prev.sentence.verb != next.sentence.verb ||
	    prev.sentence.objectA != next.sentence.objectA ||
	    prev.sentence.objectB != next.sentence.objectB ||
	    prev.sentence.active != next.sentence.active) {
		Common::String p;
		p += '{';
		kvInt(p, "verb", next.sentence.verb, true);
		kvInt(p, "objectA", next.sentence.objectA, false);
		kvInt(p, "objectB", next.sentence.objectB, false);
		kvBool(p, "active", next.sentence.active, false);
		p += '}';
		emitEvent(kEventSentenceChanged, p, nowMs);
	}

	if (prev.ego.room != next.ego.room ||
	    prev.ego.pos.x != next.ego.pos.x ||
	    prev.ego.pos.y != next.ego.pos.y) {
		// Only emit if we actually changed rooms, or if we stopped walking
		// — otherwise per-pixel motion spams the channel.
		if (prev.ego.room != next.ego.room ||
		    (prev.ego.walking && !next.ego.walking)) {
			Common::String p;
			p += '{';
			kvInt(p, "room", next.ego.room, true);
			kvInt(p, "x", next.ego.pos.x, false);
			kvInt(p, "y", next.ego.pos.y, false);
			kvBool(p, "walking", next.ego.walking, false);
			p += '}';
			emitEvent(kEventEgoMoved, p, nowMs);
		}
	}
}

void Runtime::tick(ScummEngine *engine) {
	if (!_enabled)
		return;
	if (!engine)
		return;

	Snapshot next;
	if (!Collector::capture(engine, next))
		return;

	next.seq = ++_seq;

	// Events: compare against previous snapshot to detect changes.
	if (_hasLast) {
		detectAndEmitEvents(_last, next);
	}

	// Full snapshot: rate-limit to _minPeriodMs.
	const uint32 nowMs = next.timestampMs;
	const bool timeOk = (!_hasLast) || (nowMs - _lastSnapshotMs >= _minPeriodMs);
	if (timeOk && _publisher) {
		_publisher->publishSnapshot(snapshotToJson(next));
		_lastSnapshotMs = nowMs;
	}

	_last = next;
	_hasLast = true;
}

} // namespace Agent
} // namespace Scumm
