﻿#include <pch.h>

#include "NvBlastExtPxAssetImpl.h"
#include "NvBlastHashMap.h"

#include "NvBlastAssert.h"
#include "NvBlastIndexFns.h"

#include "NvBlastTkAsset.h"

#include "foundation/PxIO.h"
#include "PxPhysics.h"
#include "filebuf/PxFileBuf.h"
#include "cooking/PxCooking.h"

#include <algorithm>

namespace Nv
{
namespace Blast
{


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//										ExtPxAssetImpl Implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ExtPxAssetImpl::ExtPxAssetImpl(const ExtPxAssetDesc& desc, TkFramework& framework)
	: m_accelerator(nullptr)
#if !defined(NV_VC) || NV_VC >= 14
	, m_defaultActorDesc { 1.0f, nullptr, 1.0f, nullptr }
{
#else
{
	m_defaultActorDesc.uniformInitialBondHealth = m_defaultActorDesc.uniformInitialLowerSupportChunkHealth = 1.0f;
	m_defaultActorDesc.initialBondHealths = m_defaultActorDesc.initialSupportChunkHealths = nullptr;
#endif
	m_tkAsset = framework.createAsset(desc);
	fillPhysicsChunks(desc.pxChunks, desc.chunkCount);
}

ExtPxAssetImpl::ExtPxAssetImpl(const TkAssetDesc& desc, ExtPxChunk* pxChunks, ExtPxSubchunk* pxSubchunks, TkFramework& framework)
	: m_accelerator(nullptr)
#if !defined(NV_VC) || NV_VC >= 14
	, m_defaultActorDesc{ 1.0f, nullptr, 1.0f, nullptr }
{
#else
{
	m_defaultActorDesc.uniformInitialBondHealth = m_defaultActorDesc.uniformInitialLowerSupportChunkHealth = 1.0f;
	m_defaultActorDesc.initialBondHealths = m_defaultActorDesc.initialSupportChunkHealths = nullptr;
#endif
	m_tkAsset = framework.createAsset(desc);
	fillPhysicsChunks(pxChunks, pxSubchunks, desc.chunkCount);
}

ExtPxAssetImpl::ExtPxAssetImpl(TkAsset* asset, ExtPxAssetDesc::ChunkDesc* chunks, uint32_t chunkCount)
	: m_tkAsset(asset)
	, m_accelerator(nullptr)
#if !defined(NV_VC) || NV_VC >= 14
	, m_defaultActorDesc{ 1.0f, nullptr, 1.0f, nullptr }
{
#else
{
	m_defaultActorDesc.uniformInitialBondHealth = m_defaultActorDesc.uniformInitialLowerSupportChunkHealth = 1.0f;
	m_defaultActorDesc.initialBondHealths = m_defaultActorDesc.initialSupportChunkHealths = nullptr;
#endif
	m_tkAsset = asset;		
	fillPhysicsChunks(chunks, chunkCount);
}


ExtPxAssetImpl::ExtPxAssetImpl(TkAsset* asset)
	: m_tkAsset(asset)
	, m_accelerator(nullptr)
#if !defined(NV_VC) || NV_VC >= 14
	, m_defaultActorDesc { 1.0f, nullptr, 1.0f, nullptr }
{
#else
{
	m_defaultActorDesc.uniformInitialBondHealth = m_defaultActorDesc.uniformInitialLowerSupportChunkHealth = 1.0f;
	m_defaultActorDesc.initialBondHealths = m_defaultActorDesc.initialSupportChunkHealths = nullptr;
#endif
}

ExtPxAssetImpl::~ExtPxAssetImpl()
{
	if (m_tkAsset)
	{
		m_tkAsset->release();
	}
}

void ExtPxAssetImpl::release()
{
	NVBLAST_DELETE(this, ExtPxAssetImpl);
}

void ExtPxAssetImpl::fillPhysicsChunks(ExtPxChunk* pxChunks, ExtPxSubchunk* pxSuchunks, uint32_t chunkCount)
{
	// count subchunks and reserve memory
	uint32_t subchunkCount = 0;
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		const auto& chunk = pxChunks[i];
		subchunkCount += static_cast<uint32_t>(chunk.subchunkCount);
	}
	m_subchunks.resize(subchunkCount);
	m_chunks.resize(chunkCount);
	memcpy(&m_subchunks.front(), pxSuchunks, sizeof(ExtPxSubchunk) * subchunkCount);
	memcpy(&m_chunks.front(), pxChunks, sizeof(ExtPxChunk) * chunkCount);
}

void ExtPxAssetImpl::fillPhysicsChunks(ExtPxAssetDesc::ChunkDesc* pxChunks, uint32_t chunkCount)
{
	// count subchunks and reserve memory
	uint32_t subchunkCount = 0;
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		const auto& chunk = pxChunks[i];
		subchunkCount += static_cast<uint32_t>(chunk.subchunkCount);
	}
	m_subchunks.reserve(subchunkCount);

	// fill chunks and subchunks
	m_chunks.resize(chunkCount);
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		const auto& chunk = pxChunks[i];
		m_chunks[i].isStatic = chunk.isStatic;
		m_chunks[i].firstSubchunkIndex = m_subchunks.size();
		m_chunks[i].subchunkCount = chunk.subchunkCount;
		for (uint32_t k = 0; k < chunk.subchunkCount; ++k)
		{
			ExtPxSubchunk subchunk =
			{
				chunk.subchunks[k].transform,
				chunk.subchunks[k].geometry
			};
			m_subchunks.pushBack(subchunk);
		}
	}
}


NV_INLINE bool serializeConvexMesh(const PxConvexMesh& convexMesh, PxCooking& cooking, Array<uint32_t>::type& indicesScratch, 
	Array<PxHullPolygon>::type hullPolygonsScratch, PxOutputStream& stream)
{
	PxConvexMeshDesc desc;
	desc.points.data = convexMesh.getVertices();
	desc.points.count = convexMesh.getNbVertices();
	desc.points.stride = sizeof(PxVec3);

	hullPolygonsScratch.resize(convexMesh.getNbPolygons());

	uint32_t indexCount = 0;
	for (uint32_t i = 0; i < convexMesh.getNbPolygons(); i++)
	{
		PxHullPolygon polygon;
		convexMesh.getPolygonData(i, polygon);
		if (polygon.mNbVerts)
		{
			indexCount = std::max<uint32_t>(indexCount, polygon.mIndexBase + polygon.mNbVerts);
		}
	}
	indicesScratch.resize(indexCount);

	for (uint32_t i = 0; i < convexMesh.getNbPolygons(); i++)
	{
		PxHullPolygon polygon;
		convexMesh.getPolygonData(i, polygon);
		for (uint32_t j = 0; j < polygon.mNbVerts; j++)
		{
			indicesScratch[polygon.mIndexBase + j] = convexMesh.getIndexBuffer()[polygon.mIndexBase + j];
		}

		hullPolygonsScratch[i] = polygon;
	}

	desc.indices.count = indexCount;
	desc.indices.data = indicesScratch.begin();
	desc.indices.stride = sizeof(uint32_t);

	desc.polygons.count = convexMesh.getNbPolygons();
	desc.polygons.data = hullPolygonsScratch.begin();
	desc.polygons.stride = sizeof(PxHullPolygon);

	auto cookingParams = PxCookingParams(PxGetPhysics().getTolerancesScale());

#if PX_GPU_BROAD_PHASE
	cookingParams.buildGPUData = true;
#endif

	cookingParams.convexMeshCookingType = PxConvexMeshCookingType::eQUICKHULL;
	cookingParams.gaussMapLimit = 32;
	cookingParams.suppressTriangleMeshRemapTable = false;
	cookingParams.midphaseDesc = PxMeshMidPhase::eBVH34;
	//cookingParams.meshPreprocessParams = PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;

	return PxCreateConvexMesh(cookingParams, desc);
}


void ExtPxAssetImpl::setUniformHealth(bool enabled)
{
	if (m_bondHealths.empty() != enabled)
	{
		if (enabled)
		{
			m_bondHealths.resizeUninitialized(0);
			m_supportChunkHealths.resizeUninitialized(0);
		}
		else
		{
			m_bondHealths.resize(m_tkAsset->getBondCount());
			m_supportChunkHealths.resize(m_tkAsset->getGraph().nodeCount);
		}
	}

	m_defaultActorDesc.initialBondHealths = enabled ? nullptr : m_bondHealths.begin();
	m_defaultActorDesc.initialSupportChunkHealths = enabled ? nullptr : m_supportChunkHealths.begin();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//										ExtPxAsset Static
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ExtPxAsset*	ExtPxAsset::create(const ExtPxAssetDesc& desc, TkFramework& framework)
{
	ExtPxAssetImpl* asset = NVBLAST_NEW(ExtPxAssetImpl)(desc, framework);
	return asset;
}

ExtPxAsset*	ExtPxAsset::create(const TkAssetDesc& desc, ExtPxChunk* pxChunks, ExtPxSubchunk* pxSubchunks, TkFramework& framework)
{
	ExtPxAssetImpl* asset = NVBLAST_NEW(ExtPxAssetImpl)(desc, pxChunks, pxSubchunks, framework);
	return asset;
}

Nv::Blast::ExtPxAsset* ExtPxAsset::create(TkAsset* tkAsset)
{
	ExtPxAssetImpl* asset = NVBLAST_NEW(ExtPxAssetImpl)(tkAsset);

	// Don't populate the chunks or subchunks!

	return asset;
}

Nv::Blast::ExtPxAsset* ExtPxAsset::create(TkAsset* tkAsset, ExtPxAssetDesc::ChunkDesc* chunks, uint32_t chunkCount)
{
	ExtPxAssetImpl* asset = NVBLAST_NEW(ExtPxAssetImpl)(tkAsset, chunks, chunkCount);
	return asset;
}

} // namespace Blast
} // namespace Nv
