// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "core/memory.h"

#include "dx/dx.h"

namespace era_engine
{
	struct dx_allocation
	{
		void* cpuPtr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuPtr;

		dx_resource resource;
		uint32 offsetInResource;
	};

	struct dx_page
	{
		dx_resource buffer;
		dx_page* next;

		uint8* cpuBasePtr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuBasePtr;

		uint64 pageSize;
		uint64 currentOffset;
	};

	struct dx_page_pool
	{
		void initialize(uint32 sizeInBytes);

		NODISCARD dx_page* getFreePage();
		void returnPage(dx_page* page);
		void reset();

		Allocator arena;

		std::mutex mutex;

		uint64 pageSize;
		dx_page* freePages;
		dx_page* usedPages;
		dx_page* lastUsedPage;

	private:
		NODISCARD dx_page* allocateNewPage();
	};

	struct dx_upload_buffer
	{
		NODISCARD dx_allocation allocate(uint64 size, uint64 alignment);
		void reset();

		dx_page_pool* pagePool = 0;
		dx_page* currentPage = 0;
	};
}