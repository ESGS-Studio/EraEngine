// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "core_api.h"

#include "core/math.h"
#include "core/reflect.h"
#include "core/bounding_volumes.h"

namespace era_engine
{
	union camera_frustum_corners
	{
		camera_frustum_corners() {};

		struct
		{
			vec3 nearTopLeft;
			vec3 nearTopRight;
			vec3 nearBottomLeft;
			vec3 nearBottomRight;
			vec3 farTopLeft;
			vec3 farTopRight;
			vec3 farBottomLeft;
			vec3 farBottomRight;
		};

		struct
		{
			vec3 corners[8];
		};

		vec3 eye;
	};

	union camera_frustum_planes
	{
		camera_frustum_planes() {};

		// Returns true, if object should be culled
		NODISCARD bool cullWorldSpaceAABB(const bounding_box& aabb) const;
		NODISCARD bool cullModelSpaceAABB(const bounding_box& aabb, const trs& transform) const;
		NODISCARD bool cullModelSpaceAABB(const bounding_box& aabb, const mat4& transform) const;

		struct
		{
			vec4 nearPlane;
			vec4 farPlane;
			vec4 leftPlane;
			vec4 rightPlane;
			vec4 topPlane;
			vec4 bottomPlane;
		};

		vec4 planes[6];
	};

	enum camera_type
	{
		camera_type_ingame,
		camera_type_calibrated,
	};

	struct ERA_CORE_API camera_projection_extents
	{
		float left, right, top, bottom; // Extents of frustum at distance 1
	};

	struct ERA_CORE_API render_camera
	{
		render_camera() = default;
		render_camera(const render_camera& other) noexcept = default;
		render_camera(render_camera&& other) noexcept = default;
		render_camera& operator=(const render_camera& other) noexcept = default;
		render_camera& operator=(render_camera&& other) noexcept = default;

		void setPositionAndRotation(vec3 position, quat rotation);
		void initializeIngame(vec3 position, quat rotation, float verticalFOV, float nearPlane, float farPlane = -1.f);
		void initializeCalibrated(vec3 position, quat rotation, uint32 width, uint32 height, float fx, float fy, float cx, float cy, float nearPlane, float farPlane = -1.f);

		void setViewport(uint32 width, uint32 height);

		void updateMatrices();

		ray generateWorldSpaceRay(float relX, float relY) const;
		ray generateViewSpaceRay(float relX, float relY) const;

		vec3 restoreViewSpacePosition(vec2 uv, float depthBufferDepth) const;
		vec3 restoreWorldSpacePosition(vec2 uv, float depthBufferDepth) const;
		float depthBufferDepthToEyeDepth(float depthBufferDepth) const;
		float eyeDepthToDepthBufferDepth(float eyeDepth) const;
		float linearizeDepthBuffer(float depthBufferDepth) const;

		camera_frustum_corners getWorldSpaceFrustumCorners(float alternativeFarPlane = 0.f) const;
		camera_frustum_planes getWorldSpaceFrustumPlanes() const;

		camera_frustum_corners getViewSpaceFrustumCorners(float alternativeFarPlane = 0.f) const;

		camera_projection_extents getProjectionExtents() const;
		float getMinProjectionExtent() const;

		render_camera getJitteredVersion(vec2 offset) const;

		quat rotation;
		vec3 position;

		float nearPlane;
		float farPlane = -1.f;

		uint32 width, height;

		camera_type type;

		float verticalFOV;
		float fx, fy, cx, cy; // For calibrated cameras

		// Derived values
		mat4 view;
		mat4 invView;

		mat4 proj;
		mat4 invProj;

		mat4 viewProj;
		mat4 invViewProj;

		float aspect;
	};
	REFLECT_STRUCT(render_camera,
		(rotation, "Rotation"),
		(position, "Position"),
		(nearPlane, "Near plane"),
		(farPlane, "Far plane"),
		(type, "Type"),
		(verticalFOV, "Vertical FOV"),
		(fx, "Fx"),
		(fy, "Fy"),
		(cx, "Cx"),
		(cy, "Cy")
	);

	camera_frustum_planes getWorldSpaceFrustumPlanes(const mat4& viewProj);
}