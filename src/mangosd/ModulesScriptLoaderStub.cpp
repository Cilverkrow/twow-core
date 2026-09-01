/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ModulesScriptLoader.h"

// The standalone definition of the extension entry point.
//
// DynamicModules.cpp calls AddModulesScripts() unconditionally, so without this
// a core built on its own does not link -- and "the core builds on its own" is
// the property the two-repository split exists to keep. Compiled only when
// TW_EXTERNAL_MODULE_LOADER is OFF; twow-repo generates a real definition with
// the same signature and turns the option on, so exactly one definition reaches
// the linker either way.
//
// Deliberately not a weak symbol. MSVC has no equivalent, and "whichever
// definition the linker happened to prefer" is not a seam anyone can reason
// about from either side of it.
void AddModulesScripts()
{
}
