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

#ifndef SCUMM_AGENT_COMMANDS_H
#define SCUMM_AGENT_COMMANDS_H

namespace Scumm {

class ScummEngine;

namespace Agent {

/**
 * Set the engine instance for agent commands.
 * Call this during ScummEngine initialization.
 */
void setCommandEngine(ScummEngine *engine);

/**
 * Clear the engine instance.
 * Call this during ScummEngine destruction.
 */
void clearCommandEngine();

/**
 * Commander class provides agent action capabilities.
 * Declared as a friend of ScummEngine to access protected members.
 */
class Commander {
public:
	/**
	 * Click a verb by its ID. Works for both action verbs and dialog choices.
	 * @param verbId The verb ID (from verbs[].id in the snapshot)
	 */
	static void clickVerb(int verbId);

	/**
	 * Click at a position in room coordinates.
	 * @param x X coordinate in room/virtual-screen space
	 * @param y Y coordinate in room/virtual-screen space
	 */
	static void clickAt(int x, int y);

	/**
	 * Walk ego to a position in room coordinates.
	 * @param x X coordinate in room space
	 * @param y Y coordinate in room space
	 */
	static void walkTo(int x, int y);

	/**
	 * Click on an object by its ID, bypassing coordinate math.
	 * This is the preferred way for agents to interact with objects
	 * since it avoids coordinate space conversion issues.
	 * @param objectId The object ID (from roomObjects[].id in the snapshot)
	 */
	static void clickObject(int objectId);

	/**
	 * Execute a complete sentence atomically: verb + objectA [+ objectB].
	 * This queues directly into the engine's sentence stack, bypassing
	 * the two-step click-verb-then-click-object pattern which is
	 * timing-sensitive and unreliable from an external agent.
	 *
	 * @param verb    Verb ID (from verbs[].id in the snapshot)
	 * @param objectA First object ID (from roomObjects[].id or inventory[].id)
	 * @param objectB Second object ID for two-object verbs like "Use X with Y", or 0
	 * @return true if the sentence was queued, false if engine not ready or queue full
	 */
	static bool doSentence(int verb, int objectA, int objectB);

	/**
	 * Dismiss any currently displayed message / actor speech.
	 * Calls the engine's stopTalk() directly, which clears _haveMsg
	 * and stops talk animation. This is needed because the normal
	 * injectClick() path doesn't reliably dismiss messages (the
	 * message system is script-driven, not input-driven).
	 *
	 * Safe to call when no message is showing (no-op).
	 */
	static void skipMessage();

private:
	Commander(); // Static methods only

	/**
	 * Inject a synthetic left-click at room coordinates into the
	 * engine's input pipeline, so processInput → checkExecVerbs
	 * handles it on the next frame with correct _virtualMouse / VARs.
	 */
	static void injectClick(int roomX, int roomY);

	/**
	 * True if an object id passed to clickObject()/doSentence() is
	 * currently reachable for the player: unset, in the player's
	 * inventory, or findable via findObject(x, y)'s filter. Needs to
	 * be a Commander member (not a free function) so it can reach
	 * ScummEngine's protected `whereIsObject` through the friend
	 * relationship.
	 */
	static bool isSentenceTargetReachable(int obj);
};

} // namespace Agent
} // namespace Scumm

#endif // SCUMM_AGENT_COMMANDS_H
