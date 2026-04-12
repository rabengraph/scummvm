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
#include "scumm/object.h"
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

	// verbs
	out += ",\"verbs\":[";
	for (uint i = 0; i < s.verbs.size(); ++i) {
		if (i)
			out += ',';
		writeVerb(out, s.verbs[i]);
	}
	out += ']';

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
	if (!engine->_verbs || engine->_numVerbs <= 0)
		return;
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

		// Verb display string is stored in rtVerb resources. We try to fetch
		// it, but fall back to an empty name if unavailable — the harness can
		// then render the id instead.
		const byte *verbText = engine->getResourceAddress(rtVerb, i);
		if (verbText) {
			vi.name = Common::String((const char *)verbText);
		}
		out.verbs.push_back(vi);
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

	fillEgo(engine, out);
	fillHover(engine, out);
	fillSentence(engine, out);
	fillRoomObjects(engine, out);
	fillInventory(engine, out);
	fillVerbs(engine, out);

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
