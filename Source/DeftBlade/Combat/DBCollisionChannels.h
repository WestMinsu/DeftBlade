#pragma once

#include "Engine/EngineTypes.h"

// Melee in DefaultEngine.ini: damageable capsules overlap, world geometry blocks.
namespace DBCollision
{
	inline constexpr ECollisionChannel Melee = ECC_GameTraceChannel1;
}
