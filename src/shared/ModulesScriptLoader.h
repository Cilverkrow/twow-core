/*
 * This file is derived from the AzerothCore Project.
 * Copyright (C) AzerothCore contributors.
 * Source: https://github.com/azerothcore/azerothcore-wotlk
 *
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

#ifndef _MODULES_SCRIPT_LOADER_H_
#define _MODULES_SCRIPT_LOADER_H_

// The core's entire C++ surface for host extensions: one function, called once
// from DynamicModules.cpp at startup, whose job is to register scripts with
// ScriptMgr before the world loads.
//
// It lives in the core rather than in the host because the core is the side
// that CALLS it. A declaration kept only in twow-repo would leave the two
// repositories agreeing on this signature by convention instead of by
// compilation, and the failure mode of disagreeing is an undefined symbol at
// the very end of a 25-minute link.
//
// src/mangosd/ModulesScriptLoaderStub.cpp defines it as a no-op unless
// TW_EXTERNAL_MODULE_LOADER says a host has generated a real one.
void AddModulesScripts();

#endif
