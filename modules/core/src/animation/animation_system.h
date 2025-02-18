#pragma once

#include "ecs/system.h"

namespace era_engine
{
	struct Allocator;
	class RendererHolderRootComponent;
}

namespace era_engine::animation
{
	class AnimationSystem final : public System
	{
	public:
		AnimationSystem(World* _world);
		~AnimationSystem();

		void init() override;
		void update(float dt) override;

		ERA_VIRTUAL_REFLECT(System)
	private:
		Allocator* allocator = nullptr;
		RendererHolderRootComponent* renderer_holder_rc = nullptr;
	};
}