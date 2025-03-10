#pragma once

#include "ecs/system.h"

namespace era_engine
{

	class RendererHolderRootComponent;

	class CameraSystem final : public System
	{
	public:
		CameraSystem(World* _world);
		~CameraSystem();

		void init() override;
		void update(float dt) override;

		ERA_VIRTUAL_REFLECT(System)

	private:
		RendererHolderRootComponent* renderer_holder_rc = nullptr;
	};
}