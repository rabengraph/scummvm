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

// Emscripten JS bridge for the bench telemetry channel. Mirrors the
// pattern in agent_bridge_emscripten.cpp but publishes to a separate
// `window.__scummBenchEmit` so the playtime channel stays untouched.
//
// The harness installs the hook before the wasm runtime starts; if it's
// missing the call is a silent no-op, so the engine can boot before the
// harness page is ready.

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "scumm/agent_bench.h"
#include "scumm/agent_state.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

namespace Scumm {
namespace Agent {
namespace Bench {

namespace {

void jsBenchEmit(const char *json) {
	// clang-format off
	EM_ASM({
		try {
			if (typeof window === 'undefined') return;
			var fn = window.__scummBenchEmit;
			if (typeof fn !== 'function') return;
			var text = UTF8ToString($0);
			var obj;
			try {
				obj = JSON.parse(text);
			} catch (parseErr) {
				if (typeof console !== 'undefined' && console.error) {
					console.error('[scummvm-bench] event JSON parse failed:', parseErr, text);
				}
				return;
			}
			fn(obj);
		} catch (e) {
			if (typeof console !== 'undefined' && console.error) {
				console.error('[scummvm-bench] __scummBenchEmit threw:', e);
			}
		}
	}, json);
	// clang-format on
}

class EmscriptenBenchPublisher : public Publisher {
public:
	virtual void publishSnapshot(const Common::String &) override {
		// Bench doesn't emit snapshots.
	}
	virtual void publishEvent(const Common::String &json) override {
		jsBenchEmit(json.c_str());
	}
};

} // anonymous namespace

Publisher *createEmscriptenBenchPublisher() {
	return new EmscriptenBenchPublisher();
}

} // namespace Bench
} // namespace Agent
} // namespace Scumm

#endif // __EMSCRIPTEN__
