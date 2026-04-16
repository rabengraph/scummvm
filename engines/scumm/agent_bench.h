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

#ifndef SCUMM_AGENT_BENCH_H
#define SCUMM_AGENT_BENCH_H

// --- ScummVM Agent Benchmark Telemetry ------------------------------------
//
// A second, narrow telemetry surface used by the agent-game-harness'
// game-agnostic benchmark. It rides next to the playtime telemetry from
// agent_state.{h,cpp} but is intentionally separate:
//
//  * Playtime telemetry (`__scummPublish` / `__scummEmit`) is shaped for
//    an agent that needs to *play* the game — verbose snapshots, hover,
//    sentence, walkboxes, etc.
//  * Benchmark telemetry (`__scummBenchEmit`) is shaped for a scoring
//    harness that needs to detect *progress* — small, raw events emitted
//    from the engine's own state-mutation funnels.
//
// Hooks are wired into the engine at the choke points already identified
// in engines/scumm/AGENT_BENCHMARK.md:
//
//    runScript / stopScript        -> kBenchScriptEntered / kBenchScriptExited
//    writeVar (scumm-vars path)    -> kBenchVarWritten
//    putState                      -> kBenchObjectStateChanged
//    putOwner                      -> kBenchOwnerChanged
//    checkAndRunSentenceScript     -> kBenchSentenceResolved
//    scummLoop                     -> kBenchTick (engine-tick monotonic)
//
// All hooks are compile-time always-on and runtime gated by Bench::enabled().
// When disabled they cost a single load+branch — safe on hot paths like
// writeVar.
// --------------------------------------------------------------------------

#include "common/scummsys.h"
#include "common/str.h"

namespace Scumm {
namespace Agent {

class Publisher;

namespace Bench {

/// Bumped on breaking changes to the bench event vocabulary or payloads.
/// Independent of Agent::kSchemaVersion so the play-time schema can evolve
/// without rev'ing the bench schema.
static const int kBenchSchemaVersion = 1;

enum BenchEventKind {
	kBenchHello = 0,           ///< One-shot at startup; payload carries schema/game.
	kBenchScriptEntered = 1,   ///< runScript dispatched a new slot.
	kBenchScriptExited = 2,    ///< stopScript killed a slot.
	kBenchVarWritten = 3,      ///< writeVar changed a scumm var.
	kBenchObjectStateChanged = 4, ///< putState changed an object's state byte.
	kBenchOwnerChanged = 5,    ///< putOwner changed an object's owner.
	kBenchSentenceResolved = 6,///< checkAndRunSentenceScript dispatched a sentence.
	kBenchTick = 7             ///< Once per scummLoop. Monotonic engine clock.
};

/// Lifecycle. Called from ScummEngine ctor/dtor next to the playtime
/// runtime setup in scumm.cpp. The publisher is owned by the bench module.
void setEnabled(bool on);
bool enabled();
void setPublisher(Publisher *p);
void shutdown();

/// Factory; mirrors Agent::createDefaultPublisher() — picks the Emscripten
/// JS bridge when built for the web, debug-log otherwise.
Publisher *createDefaultBenchPublisher();
Publisher *createDebugLogBenchPublisher();
#ifdef __EMSCRIPTEN__
Publisher *createEmscriptenBenchPublisher();
#endif

/// Emit a one-shot hello with schema + game info. Called from ScummEngine
/// ctor once the publisher is wired up.
void emitHello(int gameId, int gameVersion, const Common::String &gameName);

// --- Hook entry points (cheap no-ops when disabled) -----------------------
//
// Each hook re-checks `enabled()` internally so call sites can be a single
// unconditional function call; the engine doesn't need to scatter
// `if (Bench::enabled())` guards on hot paths.

void onScriptEntered(int scriptId, int callerScriptId, int where, bool recursive);
void onScriptExited(int scriptId);
void onVarWritten(int var, int oldValue, int newValue, int scriptId);
void onObjectStateChanged(int obj, int oldState, int newState);
void onOwnerChanged(int obj, int oldOwner, int newOwner);
void onSentenceResolved(int verb, int objectA, int objectB, int sentenceScript);

/// Emit one tick event. Called from scummLoop after the playtime runtime
/// tick. Increments the internal tick counter and stamps it into payload.
void onTick();

} // namespace Bench
} // namespace Agent
} // namespace Scumm

#endif // SCUMM_AGENT_BENCH_H
