#pragma once

#include "core_api.h"

#include "core/input.h"
#include "core/math.h"

#include "ecs/component.h"

namespace era_engine
{

	class ERA_CORE_API InputRecieverComponent final : public Component
	{
	public:
		InputRecieverComponent() = default;
		InputRecieverComponent(ref<Entity::EcsData> _data);

		~InputRecieverComponent() override;

		const vec3& get_current_input() const;

		bool is_active() const;

		const UserInput& get_frame_input() const;

		ERA_VIRTUAL_REFLECT(Component)

	private:
		UserInput frame_input{};

		vec3 current_input = vec3::zero;

		bool active = true;

		friend class InputSystem;
		friend class InputSenderComponent;
	};

}