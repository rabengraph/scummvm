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

#include "scumm/agent_bench.h"
#include "scumm/agent_state.h"

#include "common/debug.h"
#include "common/str.h"
#include "common/system.h"

namespace Scumm {
namespace Agent {
namespace Bench {

namespace {

// Single global state. There's only ever one ScummEngine instance in any
// supported build target, so a module-static singleton is the right shape
// — same pattern as agent_commands.cpp's g_commandEngine.
bool g_enabled = false;
Publisher *g_publisher = nullptr;
uint32 g_seq = 0;
uint32 g_tick = 0;

// JSON helpers — tiny, hand-rolled, no allocator surprises on the hot path.

void appendEscaped(Common::String &out, const Common::String &s) {
	out += '"';
	for (uint i = 0; i < s.size(); ++i) {
		char c = s[i];
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if ((unsigned char)c < 0x20)
				out += Common::String::format("\\u%04x", (unsigned char)c);
			else
				out += c;
		}
	}
	out += '"';
}

void emit(int kind, const Common::String &payloadJson) {
	if (!g_publisher)
		return;
	const uint32 nowMs = g_system ? g_system->getMillis() : 0;
	Common::String out;
	out += Common::String::format(
	    "{\"kind\":%d,\"seq\":%u,\"t\":%u,\"tick\":%u,\"payload\":",
	    kind, ++g_seq, nowMs, g_tick);
	if (payloadJson.empty())
		out += "null";
	else
		out += payloadJson;
	out += '}';
	g_publisher->publishEvent(out);
}

class DebugLogBenchPublisher : public Publisher {
public:
	virtual void publishSnapshot(const Common::String &) override {}
	virtual void publishEvent(const Common::String &json) override {
		debug(5, "[SCUMM_BENCH] %s", json.c_str());
	}
};

} // anonymous namespace

void setEnabled(bool on) {
	g_enabled = on;
}

bool enabled() {
	return g_enabled;
}

void setPublisher(Publisher *p) {
	if (g_publisher == p)
		return;
	delete g_publisher;
	g_publisher = p;
}

void shutdown() {
	delete g_publisher;
	g_publisher = nullptr;
	g_enabled = false;
	g_seq = 0;
	g_tick = 0;
}

Publisher *createDebugLogBenchPublisher() {
	return new DebugLogBenchPublisher();
}

Publisher *createDefaultBenchPublisher() {
#ifdef __EMSCRIPTEN__
	return createEmscriptenBenchPublisher();
#else
	return createDebugLogBenchPublisher();
#endif
}

void emitHello(int gameId, int gameVersion, const Common::String &gameName) {
	if (!g_enabled)
		return;
	Common::String p;
	p += Common::String::format(
	    "{\"schema\":%d,\"gameId\":%d,\"gameVersion\":%d,\"gameName\":",
	    kBenchSchemaVersion, gameId, gameVersion);
	appendEscaped(p, gameName);
	p += '}';
	emit(kBenchHello, p);
}

void onScriptEntered(int scriptId, int callerScriptId, int where, bool recursive) {
	if (!g_enabled)
		return;
	emit(kBenchScriptEntered,
	     Common::String::format(
	         "{\"id\":%d,\"caller\":%d,\"where\":%d,\"recursive\":%s}",
	         scriptId, callerScriptId, where, recursive ? "true" : "false"));
}

void onScriptExited(int scriptId) {
	if (!g_enabled)
		return;
	emit(kBenchScriptExited,
	     Common::String::format("{\"id\":%d}", scriptId));
}

void onVarWritten(int var, int oldValue, int newValue, int scriptId) {
	if (!g_enabled)
		return;
	emit(kBenchVarWritten,
	     Common::String::format(
	         "{\"var\":%d,\"old\":%d,\"new\":%d,\"script\":%d}",
	         var, oldValue, newValue, scriptId));
}

void onObjectStateChanged(int obj, int oldState, int newState) {
	if (!g_enabled)
		return;
	emit(kBenchObjectStateChanged,
	     Common::String::format(
	         "{\"obj\":%d,\"old\":%d,\"new\":%d}",
	         obj, oldState, newState));
}

void onOwnerChanged(int obj, int oldOwner, int newOwner) {
	if (!g_enabled)
		return;
	emit(kBenchOwnerChanged,
	     Common::String::format(
	         "{\"obj\":%d,\"old\":%d,\"new\":%d}",
	         obj, oldOwner, newOwner));
}

void onSentenceResolved(int verb, int objectA, int objectB, int sentenceScript) {
	if (!g_enabled)
		return;
	emit(kBenchSentenceResolved,
	     Common::String::format(
	         "{\"verb\":%d,\"objectA\":%d,\"objectB\":%d,\"script\":%d}",
	         verb, objectA, objectB, sentenceScript));
}

void onTick() {
	if (!g_enabled)
		return;
	++g_tick;
	emit(kBenchTick, Common::String());
}

} // namespace Bench
} // namespace Agent
} // namespace Scumm
