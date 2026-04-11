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

// --- ScummVM Agent Telemetry -- Emscripten JS bridge ----------------------
//
// Publishes snapshots and events to JavaScript in an Emscripten/WebAssembly
// build.
//
// Contract with the agent-game-harness app shell
// ------------------------------------------------
// The engine calls two JS hooks installed on `window` by the harness's
// `web/shared/bridge.js` before the wasm runtime starts:
//
//     window.__scummPublish(snapshotObject)   // full-state snapshot
//     window.__scummEmit(eventObject)         // small typed change event
//
// Both receive a *parsed JavaScript object*, not a JSON string — the
// harness bridge spreads the object directly (see its publish() / emit()).
// We JSON.parse on the C++ side of the EM_ASM boundary so the harness
// stays schema-ignorant.
//
// If the hooks are missing (harness hasn't installed bridge.js yet, or
// the fork is running in a non-harness page), the calls are silent
// no-ops — so the engine can start before the harness page has mounted.
// --------------------------------------------------------------------------

// This translation unit has to include <emscripten.h>, which declares
// `emscripten_get_preloaded_image_data_from_FILE(FILE *, ...)`. ScummVM's
// common/forbidden.h macro-poisons `FILE` (and friends) to force engines
// onto Common::File, which breaks that declaration at compile time. Opt
// this file — and only this file — out of the guards so the emscripten
// header can be included cleanly. This is the standard ScummVM escape
// hatch used by other backend bridge files.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "scumm/agent_state.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

namespace Scumm {
namespace Agent {

namespace {

// We look up `window.__scummPublish` / `window.__scummEmit` on every call
// rather than caching, so the harness can install or replace them at any
// point in its lifetime (e.g. after bridge.js is loaded, or after
// navigating to /game).

void jsPublishSnapshot(const char *json) {
	// clang-format off
	EM_ASM({
		try {
			if (typeof window === 'undefined') return;
			var fn = window.__scummPublish;
			if (typeof fn !== 'function') return;
			var text = UTF8ToString($0);
			var obj;
			try {
				obj = JSON.parse(text);
			} catch (parseErr) {
				if (typeof console !== 'undefined' && console.error) {
					console.error('[scummvm-agent] snapshot JSON parse failed:', parseErr, text);
				}
				return;
			}
			fn(obj);
		} catch (e) {
			// Swallow — the engine must not crash because of a broken hook.
			if (typeof console !== 'undefined' && console.error) {
				console.error('[scummvm-agent] __scummPublish threw:', e);
			}
		}
	}, json);
	// clang-format on
}

void jsPublishEvent(const char *json) {
	// clang-format off
	EM_ASM({
		try {
			if (typeof window === 'undefined') return;
			var fn = window.__scummEmit;
			if (typeof fn !== 'function') return;
			var text = UTF8ToString($0);
			var obj;
			try {
				obj = JSON.parse(text);
			} catch (parseErr) {
				if (typeof console !== 'undefined' && console.error) {
					console.error('[scummvm-agent] event JSON parse failed:', parseErr, text);
				}
				return;
			}
			fn(obj);
		} catch (e) {
			if (typeof console !== 'undefined' && console.error) {
				console.error('[scummvm-agent] __scummEmit threw:', e);
			}
		}
	}, json);
	// clang-format on
}

class EmscriptenPublisher : public Publisher {
public:
	virtual void publishSnapshot(const Common::String &json) override {
		jsPublishSnapshot(json.c_str());
	}
	virtual void publishEvent(const Common::String &json) override {
		jsPublishEvent(json.c_str());
	}
};

} // anonymous namespace

Publisher *createEmscriptenPublisher() {
	return new EmscriptenPublisher();
}

} // namespace Agent
} // namespace Scumm

#endif // __EMSCRIPTEN__
