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

// ---------------------------------------------------------------------------
// Instead of calling runInputScript() directly (which bypasses
// processInput → checkExecVerbs and leaves _virtualMouse / VARs stale),
// we set _mouse to the correct *screen* coordinates and raise the
// left-button-clicked flag.  The engine's own main-loop then runs:
//
//   processInput()          – computes _virtualMouse from _mouse
//   scummLoop_updateScummVars() – copies _virtualMouse → VAR_VIRT_MOUSE_X/Y
//   checkExecVerbs()        – dispatches runInputScript with correct VARs
//
// This guarantees the walk target the SCUMM scripts read is consistent.
//
// All helpers live inside Commander so they have friend access to
// ScummEngine's protected members.
// ---------------------------------------------------------------------------

void Commander::injectClick(int roomX, int roomY) {
	// Convert room coords → screen coords for _mouse.
	// processInput() does the inverse: _virtualMouse.x = _mouse.x + vs->xstart
	VirtScreen *vs = &g_commandEngine->_virtscr[kMainVirtScreen];
	int screenX = roomX - vs->xstart;
	int screenY = roomY + vs->topline;

	screenX = CLIP(screenX, 0, (int)g_commandEngine->_screenWidth - 1);
	screenY = CLIP(screenY, 0, (int)g_commandEngine->_screenHeight - 1);

	g_commandEngine->_mouse.x = screenX;
	g_commandEngine->_mouse.y = screenY;

	// msClicked = 2 (see input.cpp MouseButtonStatus enum)
	g_commandEngine->_leftBtnPressed |= 2;
}

void Commander::clickAt(int x, int y) {
	if (!g_commandEngine)
		return;

	injectClick(x, y);
}

void Commander::walkTo(int x, int y) {
	if (!g_commandEngine)
		return;

	injectClick(x, y);
}

void Commander::clickObject(int objectId) {
	if (!g_commandEngine)
		return;

	// Get object center coordinates (room space)
	int x, y;
	g_commandEngine->getObjectXYPos(objectId, x, y);

	if (x == 0 && y == 0) {
		// Object not found or has no position - try direct click anyway
		g_commandEngine->runInputScript(kSceneClickArea, objectId, 1);
		return;
	}

	injectClick(x, y);
}

bool Commander::doSentence(int verb, int objectA, int objectB) {
	if (!g_commandEngine)
		return false;

	// Guard against queue overflow (NUM_SENTENCE = 6).
	if (g_commandEngine->_sentenceNum >= NUM_SENTENCE)
		return false;

	// Delegate to the engine's own doSentence(), which pushes onto the
	// _sentence[] stack. checkAndRunSentenceScript() will pop and
	// execute it on the next frame — no timing races.
	g_commandEngine->doSentence(verb, objectA, objectB);
	return true;
}

void Commander::skipMessage() {
	if (!g_commandEngine)
		return;

	// stopTalk() clears _haveMsg, stops actor talk animation, and
	// resets the charset state. Safe to call when nothing is playing.
	g_commandEngine->stopTalk();
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

/**
 * Click on an object by its ID. This is the preferred method for agents
 * since it bypasses coordinate space conversions entirely - the engine
 * looks up the object position and handles the click internally.
 *
 * @param objectId The object ID (from roomObjects[].id in the snapshot)
 */
EMSCRIPTEN_KEEPALIVE
void agent_click_object(int objectId) {
	Scumm::Agent::Commander::clickObject(objectId);
}

/**
 * Execute a complete sentence atomically: verb + objectA [+ objectB].
 * Queues directly into the engine's sentence stack — no timing races.
 *
 * @param verb    Verb ID (from verbs[].id in the snapshot)
 * @param objectA First object ID (from roomObjects[].id or inventory[].id)
 * @param objectB Second object ID for two-object verbs (e.g. "Use X with Y"), or 0
 * @return 1 if queued successfully, 0 if engine not ready or sentence queue full
 */
EMSCRIPTEN_KEEPALIVE
int agent_do_sentence(int verb, int objectA, int objectB) {
	return Scumm::Agent::Commander::doSentence(verb, objectA, objectB) ? 1 : 0;
}

/**
 * Dismiss any currently displayed message or actor speech.
 * Call this to advance past dialog text. Safe to call when no
 * message is showing (no-op).
 */
EMSCRIPTEN_KEEPALIVE
void agent_skip_message() {
	Scumm::Agent::Commander::skipMessage();
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
