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

private:
	Commander(); // Static methods only
};

} // namespace Agent
} // namespace Scumm

#endif // SCUMM_AGENT_COMMANDS_H
