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

// --- ScummVM Agent Commands -- Emscripten JS to C++ bridge -----------------
//
// Provides an action API for AI agents to control the game. These functions
// are exported to JavaScript via Emscripten's EMSCRIPTEN_KEEPALIVE and can
// be called as Module._agent_* from the browser.
//
// Contract with the agent-game-harness app shell
// ------------------------------------------------
// The harness's `web/shared/bridge.js` wraps these as:
//
//     window.__scummClickVerb(verbId)     - click a verb/dialog choice
//     window.__scummClickAt(x, y)         - click at room coordinates
//
// The engine must call Agent::setCommandEngine() during initialization
// to provide access to the ScummEngine instance.
// --------------------------------------------------------------------------

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "scumm/agent_commands.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

namespace Scumm {
namespace Agent {

// Engine pointer set during ScummEngine initialization.
// This is safe because there's only ever one ScummEngine instance
// in the Emscripten/browser context.
static ScummEngine *g_commandEngine = nullptr;

void setCommandEngine(ScummEngine *engine) {
	g_commandEngine = engine;
}

void clearCommandEngine() {
	g_commandEngine = nullptr;
}

// Commander class with friend access to ScummEngine protected members.
// See declaration in agent_commands.h and friend in scumm.h.
void Commander::clickVerb(int verbId) {
	if (!g_commandEngine)
		return;

	// kVerbClickArea = 1, code = 1 (left click)
	g_commandEngine->runInputScript(kVerbClickArea, verbId, 1);
}

void Commander::clickAt(int x, int y) {
	if (!g_commandEngine)
		return;

	// Update the virtual mouse position
	g_commandEngine->_mouse.x = x;
	g_commandEngine->_mouse.y = y;

	// Find if there's an object at this position
	int obj = g_commandEngine->findObject(x, y);

	if (obj > 0) {
		// Click on object - this will use the currently selected verb
		g_commandEngine->runInputScript(kSceneClickArea, obj, 1);
	} else {
		// Click on empty scene - typically triggers walk-to
		g_commandEngine->runInputScript(kSceneClickArea, 0, 1);
	}
}

void Commander::walkTo(int x, int y) {
	if (!g_commandEngine)
		return;

	// Update mouse position
	g_commandEngine->_mouse.x = x;
	g_commandEngine->_mouse.y = y;

	// Click on scene with no object - triggers walk
	g_commandEngine->runInputScript(kSceneClickArea, 0, 1);
}

} // namespace Agent
} // namespace Scumm

extern "C" {

/**
 * Click a verb by its ID. This works for both action verbs (Open, Close, etc.)
 * and dialog choices, since SCUMM uses the same verb system for both.
 *
 * @param verbId The verb ID to click (from verbs[].id in the snapshot)
 */
EMSCRIPTEN_KEEPALIVE
void agent_click_verb(int verbId) {
	Scumm::Agent::Commander::clickVerb(verbId);
}

/**
 * Click at a position in room coordinates. The engine will determine what
 * object (if any) is at that position and execute the appropriate action.
 *
 * @param x X coordinate in room/virtual-screen space
 * @param y Y coordinate in room/virtual-screen space
 */
EMSCRIPTEN_KEEPALIVE
void agent_click_at(int x, int y) {
	Scumm::Agent::Commander::clickAt(x, y);
}

/**
 * Walk the ego actor to a position in room coordinates.
 * This is equivalent to clicking with "Walk to" verb selected.
 *
 * @param x X coordinate in room space
 * @param y Y coordinate in room space
 */
EMSCRIPTEN_KEEPALIVE
void agent_walk_to(int x, int y) {
	Scumm::Agent::Commander::walkTo(x, y);
}

} // extern "C"

#else // !__EMSCRIPTEN__

// Stub implementations for non-Emscripten builds
namespace Scumm {
namespace Agent {

void setCommandEngine(ScummEngine *) {}
void clearCommandEngine() {}

} // namespace Agent
} // namespace Scumm

#endif // __EMSCRIPTEN__
