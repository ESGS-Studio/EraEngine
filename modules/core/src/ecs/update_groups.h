#pragma once

#include "ecs/system.h"

namespace era_engine::update_types
{
	static inline UpdateGroup INPUT = UpdateGroup("INPUT", UpdateType::NORMAL);

	static inline UpdateGroup BEGIN = UpdateGroup("BEGIN", UpdateType::NORMAL);

	static inline UpdateGroup BEFORE_PHYSICS = UpdateGroup("BEFORE_PHYSICS", UpdateType::FIXED);
	static inline UpdateGroup PHYSICS = UpdateGroup("PHYSICS", UpdateType::FIXED);
	static inline UpdateGroup AFTER_PHYSICS = UpdateGroup("AFTER_PHYSICS", UpdateType::FIXED);

	static inline UpdateGroup BEFORE_RENDER = UpdateGroup("BEFORE_RENDER", UpdateType::NORMAL);
	static inline UpdateGroup RENDER = UpdateGroup("RENDER", UpdateType::NORMAL);
	static inline UpdateGroup AFTER_RENDER = UpdateGroup("AFTER_RENDER", UpdateType::NORMAL);

	static inline UpdateGroup END = UpdateGroup("END", UpdateType::NORMAL);
}