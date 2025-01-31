// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "core_api.h"

#include "window/window.h"

namespace era_engine
{
	struct ERA_CORE_API software_window : win32_window
	{
		software_window() = default;
		software_window(software_window&) = delete;
		software_window(software_window&& o) noexcept;

		~software_window();

		bool initialize(const TCHAR* name, uint32 requestedClientWidth, uint32 requestedClientHeight,
			uint8* buffer, uint32 numChannels, uint32 bufferWidth = 0, uint32 bufferHeight = 0);

		void changeBlitRegion(uint32 x, uint32 y, uint32 width, uint32 height);

		virtual void shutdown();

		void swapBuffers() override;

		BITMAPINFO* bitmapInfo = 0;
		const uint8* buffer = 0;

		uint32 blitX;
		uint32 blitY;
		uint32 blitWidth;
		uint32 blitHeight;
	};
}