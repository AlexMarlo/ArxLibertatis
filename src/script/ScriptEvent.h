/*
 * Copyright 2011-2012 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARX_SCRIPT_SCRIPTEVENT_H
#define ARX_SCRIPT_SCRIPTEVENT_H

#include <map>

#include "script/Script.h"

struct SCRIPT_EVENT {
	explicit SCRIPT_EVENT(const std::string & str): name(str) {}
	std::string name;
};

namespace script {

//! strip [] brackets
std::string loadUnlocalized(const std::string & str);

class Command;

} // namespace script

class ScriptEvent {
	
public:
	
	static long totalCount;
	
	ScriptEvent();
	virtual ~ScriptEvent();
	
	static ScriptResult send(EERIE_SCRIPT * es, ScriptMessage msg, const std::string & params, Entity * io, const std::string & eventname, long info = 0);
	
	static void registerCommand(script::Command * command);
	
	static void init();
	
private:
	
	typedef std::map<std::string, script::Command *> Commands;
	static Commands commands;
	
};

#endif // ARX_SCRIPT_SCRIPTEVENT_H
