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
// This file's *only* contract with the outside world is a single global
// JS hook:
//
//     globalThis.__scummvmAgentHook
//
// It is the fork's native bridge surface. It is intentionally generic and
// NOT named after any particular harness field. A thin adapter in the
// harness repo is expected to wrap it and expose whatever JS names the
// harness wants (`window.__scummState`, `#scumm-state`,
// `[SCUMM_STATE]` console tags, etc.).
//
// Hook shape (JavaScript):
//
//     globalThis.__scummvmAgentHook = {
//         // Called with a JSON string containing a full snapshot whenever
//         // the engine publishes one (rate-limited, ~10 Hz).
//         onSnapshot(json) {},
//
//         // Called with a JSON string containing a small typed event.
//         onEvent(json) {},
//     };
//
// If `__scummvmAgentHook` is absent or a method is missing, the call is a
// no-op — so the engine can start before the harness page has attached.
// --------------------------------------------------------------------------

#include "scumm/agent_state.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

namespace Scumm {
namespace Agent {

namespace {

// We pass the JSON payload into JavaScript land via EM_ASM_. The JS side
// receives a UTF-8 C-string pointer; UTF8ToString decodes it.
//
// We look up `globalThis.__scummvmAgentHook` each time rather than caching,
// so the harness can install/replace the hook at any point in its lifetime
// (e.g. after the page has mounted).

void jsPublishSnapshot(const char *json) {
	// clang-format off
	EM_ASM({
		try {
			var hook = (typeof globalThis !== 'undefined') ? globalThis.__scummvmAgentHook : null;
			if (hook && typeof hook.onSnapshot === 'function') {
				hook.onSnapshot(UTF8ToString($0));
			}
		} catch (e) {
			// Swallow — the engine must not crash because of a broken hook.
			if (typeof console !== 'undefined' && console.error) {
				console.error('[scummvm-agent] onSnapshot threw:', e);
			}
		}
	}, json);
	// clang-format on
}

void jsPublishEvent(const char *json) {
	// clang-format off
	EM_ASM({
		try {
			var hook = (typeof globalThis !== 'undefined') ? globalThis.__scummvmAgentHook : null;
			if (hook && typeof hook.onEvent === 'function') {
				hook.onEvent(UTF8ToString($0));
			}
		} catch (e) {
			if (typeof console !== 'undefined' && console.error) {
				console.error('[scummvm-agent] onEvent threw:', e);
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
