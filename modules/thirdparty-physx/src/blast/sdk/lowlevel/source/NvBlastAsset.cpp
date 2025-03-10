// This code contains NVIDIA Confidential Information and is disclosed to you
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and
// any modifications thereto. Any use, reproduction, disclosure, or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA Corporation is strictly prohibited.
//
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2016-2020 NVIDIA Corporation. All rights reserved.
#include <pch.h>


#include "NvBlastAssert.h"
#include "NvBlastAsset.h"
#include "NvBlastActor.h"
#include "NvBlastMath.h"
#include "NvBlastPreprocessorInternal.h"
#include "NvBlastIndexFns.h"
#include "NvBlastActorSerializationBlock.h"
#include "NvBlastMemory.h"

#include <algorithm>


namespace Nv
{
namespace Blast
{


//////// Local helper functions ////////


/**
Helper function to validate the input parameters for NvBlastCreateAsset.  See NvBlastCreateAsset for parameter definitions.
*/
static bool solverAssetBuildValidateInput(void* mem, const NvBlastAssetDesc* desc, void* scratch, NvBlastLog logFn)
{
	if (mem == nullptr)
	{
		NVBLASTLL_LOG_ERROR(logFn, "AssetBuildValidateInput: NULL mem pointer input.");
		return false;
	}

	if (desc == nullptr)
	{
		NVBLASTLL_LOG_ERROR(logFn, "AssetBuildValidateInput: NULL desc pointer input.");
		return false;
	}

	if (desc->chunkCount == 0)
	{
		NVBLASTLL_LOG_ERROR(logFn, "AssetBuildValidateInput: Zero chunk count not allowed.");
		return false;
	}

	if (desc->chunkDescs == nullptr)
	{
		NVBLASTLL_LOG_ERROR(logFn, "AssetBuildValidateInput: NULL chunkDescs pointer input.");
		return false;
	}

	if (desc->bondCount != 0 && desc->bondDescs == nullptr)
	{
		NVBLASTLL_LOG_ERROR(logFn, "AssetBuildValidateInput: bondCount non-zero but NULL bondDescs pointer input.");
		return false;
	}

	if (scratch == nullptr)
	{
		NVBLASTLL_LOG_ERROR(logFn, "AssetBuildValidateInput: NULL scratch pointer input.");
		return false;
	}

	return true;
}


struct AssetDataOffsets
{
	size_t m_chunks;
	size_t m_bonds;
	size_t m_subtreeLeafChunkCounts;
	size_t m_supportChunkIndices;
	size_t m_chunkToGraphNodeMap;
	size_t m_graphAdjacencyPartition;
	size_t m_graphAdjacentNodeIndices;
	size_t m_graphAdjacentBondIndices;
};


static size_t createAssetDataOffsets(AssetDataOffsets& offsets, uint32_t chunkCount, uint32_t graphNodeCount, uint32_t bondCount)
{
	NvBlastCreateOffsetStart(sizeof(Asset));
	NvBlastCreateOffsetAlign16(offsets.m_chunks, chunkCount * sizeof(NvBlastChunk));
	NvBlastCreateOffsetAlign16(offsets.m_bonds, bondCount * sizeof(NvBlastBond));
	NvBlastCreateOffsetAlign16(offsets.m_subtreeLeafChunkCounts, chunkCount * sizeof(uint32_t));
	NvBlastCreateOffsetAlign16(offsets.m_supportChunkIndices, graphNodeCount * sizeof(uint32_t));
	NvBlastCreateOffsetAlign16(offsets.m_chunkToGraphNodeMap, chunkCount * sizeof(uint32_t));
	NvBlastCreateOffsetAlign16(offsets.m_graphAdjacencyPartition, (graphNodeCount + 1) * sizeof(uint32_t));
	NvBlastCreateOffsetAlign16(offsets.m_graphAdjacentNodeIndices, (2 * bondCount) * sizeof(uint32_t));
	NvBlastCreateOffsetAlign16(offsets.m_graphAdjacentBondIndices, (2 * bondCount) * sizeof(uint32_t));
	return NvBlastCreateOffsetEndAlign16();
}


Asset* initializeAsset(void* mem, NvBlastID id, uint32_t chunkCount, uint32_t graphNodeCount, uint32_t leafChunkCount, uint32_t firstSubsupportChunkIndex, uint32_t bondCount, NvBlastLog logFn)
{
	// Data offsets
	AssetDataOffsets offsets;
	const size_t dataSize = createAssetDataOffsets(offsets, chunkCount, graphNodeCount, bondCount);

	// Restricting our data size to < 4GB so that we may use uint32_t offsets
	if (dataSize > (size_t)UINT32_MAX)
	{
		NVBLASTLL_LOG_ERROR(logFn, "Nv::Blast::allocateAsset: Asset data size will exceed 4GB.  Instance not created.\n");
		return nullptr;
	}

	// Zero memory and cast to Asset
	Asset* asset = reinterpret_cast<Asset*>(memset(mem, 0, dataSize));

	// Fill in fields
	const size_t graphOffset = NV_OFFSET_OF(Asset, m_graph);
	asset->m_header.dataType = NvBlastDataBlock::AssetDataBlock;
	asset->m_header.formatVersion = 0;	// Not currently using this field
	asset->m_header.size = (uint32_t)dataSize;
	asset->m_header.reserved = 0;
	asset->m_ID = id;
	asset->m_chunkCount = chunkCount;
	asset->m_graph.m_nodeCount = graphNodeCount;
	asset->m_graph.m_chunkIndicesOffset = (uint32_t)(offsets.m_supportChunkIndices - graphOffset);
	asset->m_graph.m_adjacencyPartitionOffset = (uint32_t)(offsets.m_graphAdjacencyPartition - graphOffset);
	asset->m_graph.m_adjacentNodeIndicesOffset = (uint32_t)(offsets.m_graphAdjacentNodeIndices - graphOffset);
	asset->m_graph.m_adjacentBondIndicesOffset = (uint32_t)(offsets.m_graphAdjacentBondIndices - graphOffset);
	asset->m_leafChunkCount = leafChunkCount;
	asset->m_firstSubsupportChunkIndex = firstSubsupportChunkIndex;
	asset->m_bondCount = bondCount;
	asset->m_chunksOffset = (uint32_t)offsets.m_chunks;
	asset->m_bondsOffset = (uint32_t)offsets.m_bonds;
	asset->m_subtreeLeafChunkCountsOffset = (uint32_t)offsets.m_subtreeLeafChunkCounts;
	asset->m_chunkToGraphNodeMapOffset = (uint32_t)offsets.m_chunkToGraphNodeMap;

	// Ensure Bonds remain aligned
	NV_COMPILE_TIME_ASSERT((sizeof(NvBlastBond) & 0xf) == 0);

	// Ensure Bonds are aligned - note, this requires that the block be aligned
	NVBLAST_ASSERT((uintptr_t(asset->getBonds()) & 0xf) == 0);

	return asset;
}


/**
Tests for a loop in a digraph starting at a given graph vertex.

Using the implied digraph given by the chunkDescs' parentChunkIndex fields, the graph is walked from the chunk descriptor chunkDescs[chunkIndex],
to determine if that walk leads to a loop.

Input:
chunkDescs	- the chunk descriptors
chunkIndex	- the index of the starting chunk descriptor

Return:
true if a loop is found, false otherwise.
*/
NV_INLINE bool testForLoop(const NvBlastChunkDesc* chunkDescs, uint32_t chunkIndex)
{
	NVBLAST_ASSERT(!isInvalidIndex(chunkIndex));

	uint32_t chunkIndex1 = chunkDescs[chunkIndex].parentChunkDescIndex;
	if (isInvalidIndex(chunkIndex1))
	{
		return false;
	}

	uint32_t chunkIndex2 = chunkDescs[chunkIndex1].parentChunkDescIndex;
	if (isInvalidIndex(chunkIndex2))
	{
		return false;
	}

	do
	{
		// advance index 1
		chunkIndex1 = chunkDescs[chunkIndex1].parentChunkDescIndex;	// No need to check for termination here.  index 2 would find it first.

		// advance index 2 twice and check for incidence with index 1 as well as termination
		if ((chunkIndex2 = chunkDescs[chunkIndex2].parentChunkDescIndex) == chunkIndex1)
		{
			return true;
		}
		if (isInvalidIndex(chunkIndex2))
		{
			return false;
		}
		if ((chunkIndex2 = chunkDescs[chunkIndex2].parentChunkDescIndex) == chunkIndex1)
		{
			return true;
		}
	} while (!isInvalidIndex(chunkIndex2));

	return false;
}


/**
Tests a set of chunk descriptors to see if the implied hierarchy describes valid trees.

A single tree implies that only one of the chunkDescs has an invalid (invalidIndex<uint32_t>()) parentChunkIndex, and all other
chunks are descendents of that chunk. Passed set of chunk is checked to contain one or more single trees.

Input:
chunkCount	- the number of chunk descriptors
chunkDescs	- an array of chunk descriptors of length chunkCount
logFn		- message function (see NvBlastLog definition).

Return:
true if the descriptors imply a valid trees, false otherwise.
*/
static bool testForValidTrees(uint32_t chunkCount, const NvBlastChunkDesc* chunkDescs, NvBlastLog logFn)
{
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		// Ensure there are no loops
		if (testForLoop(chunkDescs, i))
		{
			NVBLASTLL_LOG_WARNING(logFn, "testForValidTrees: loop found.  Asset will not be created.");
			return false;
		}
	}

	return true;
}


/**
Struct to hold chunk indices and bond index for sorting

Utility struct used by NvBlastCreateAsset in order to arrange bond data in a lookup table, and also to easily identify redundant input.
*/
struct BondSortData	
{
	BondSortData(uint32_t c0, uint32_t c1, uint32_t b) : m_c0(c0), m_c1(c1), m_b(b) {}

	uint32_t	m_c0;
	uint32_t	m_c1;
	uint32_t	m_b;
};


/**
Functional class for sorting a list of BondSortData
*/
class BondsOrdered
{
public:
	bool	operator () (const BondSortData& bond0, const BondSortData& bond1) const
	{
		return (bond0.m_c0 != bond1.m_c0) ? (bond0.m_c0 < bond1.m_c0) : (bond0.m_c1 != bond1.m_c1 ? bond0.m_c1 < bond1.m_c1 : bond0.m_b < bond1.m_b);
	}
};


//////// Asset static functions ////////

size_t Asset::getMemorySize(const NvBlastAssetDesc* desc)
{
	NVBLAST_ASSERT(desc != nullptr);

	// Count graph nodes
	uint32_t graphNodeCount = 0;
	for (uint32_t i = 0; i < desc->chunkCount; ++i)
	{
		graphNodeCount += (uint32_t)((desc->chunkDescs[i].flags & NvBlastChunkDesc::SupportFlag) != 0);
	}

	for (uint32_t i = 0; i < desc->bondCount; ++i)
	{
		const NvBlastBondDesc& bondDesc = desc->bondDescs[i];
		const uint32_t chunkIndex0 = bondDesc.chunkIndices[0];
		const uint32_t chunkIndex1 = bondDesc.chunkIndices[1];
		if ((isInvalidIndex(chunkIndex0) && chunkIndex1 < desc->chunkCount) ||
			(isInvalidIndex(chunkIndex1) && chunkIndex0 < desc->chunkCount))
		{
			++graphNodeCount;	// world node
			break;
		}
	}

	AssetDataOffsets offsets;
	return createAssetDataOffsets(offsets, desc->chunkCount, graphNodeCount, desc->bondCount);
}


size_t Asset::createRequiredScratch(const NvBlastAssetDesc* desc)
{
#if NVBLASTLL_CHECK_PARAMS
	if (desc == nullptr)
	{
		NVBLAST_ALWAYS_ASSERT();
		return 0;
	}
#endif

	// Aligned and padded
	return 16 +
		align16(desc->chunkCount*sizeof(char)) +
		align16(desc->chunkCount*sizeof(uint32_t)) +
		align16(2 * desc->bondCount*sizeof(BondSortData)) +
		align16(desc->bondCount*sizeof(uint32_t));
}


Asset* Asset::create(void* mem, const NvBlastAssetDesc* desc, void* scratch, NvBlastLog logFn)
{
#if NVBLASTLL_CHECK_PARAMS
	if (!solverAssetBuildValidateInput(mem, desc, scratch, logFn))
	{
		return nullptr;
	}
#else 
	NV_UNUSED(solverAssetBuildValidateInput);
#endif

	NVBLASTLL_CHECK((reinterpret_cast<uintptr_t>(mem) & 0xF) == 0, logFn, "NvBlastCreateAsset: mem pointer not 16-byte aligned.", return nullptr);

	// Make sure we have valid trees before proceeding
	if (!testForValidTrees(desc->chunkCount, desc->chunkDescs, logFn))
	{
		return nullptr;
	}

	scratch = (void*)align16((size_t)scratch);	// Bump to 16-byte alignment (see padding in NvBlastGetRequiredScratchForCreateAsset)

	// reserve chunkAnnotation on scratch
	char* chunkAnnotation = reinterpret_cast<char*>(scratch); scratch = pointerOffset(scratch, align16(desc->chunkCount));

	// test for coverage, chunkAnnotation will be filled there.
	uint32_t leafChunkCount;
	uint32_t supportChunkCount;
	if (!ensureExactSupportCoverage(supportChunkCount, leafChunkCount, chunkAnnotation, desc->chunkCount, const_cast<NvBlastChunkDesc*>(desc->chunkDescs), true, logFn))
	{
		NVBLASTLL_LOG_ERROR(logFn, "NvBlastCreateAsset: support coverage is not exact.  Asset will not be created.  The Asset helper function NvBlastEnsureAssetExactSupportCoverage may be used to create exact coverage.");
		return nullptr;
	}

	// test for valid chunk order
	if (!testForValidChunkOrder(desc->chunkCount, desc->chunkDescs, chunkAnnotation, scratch))
	{
		NVBLASTLL_LOG_ERROR(logFn, "NvBlastCreateAsset: chunks order is invalid.  Asset will not be created.  Use Asset helper functions such as NvBlastBuildAssetDescChunkReorderMap to fix descriptor order.");
		return nullptr;
	}

	// Find first subsupport chunk
	uint32_t firstSubsupportChunkIndex = desc->chunkCount;	// Set value to chunk count if no subsupport chunks are found
	for (uint32_t i = 0; i < desc->chunkCount; ++i)
	{
		if ((chunkAnnotation[i] & ChunkAnnotation::UpperSupport) == 0)
		{
			firstSubsupportChunkIndex = i;
			break;
		}
	}

	// Create map from global indices to graph node indices and initialize to invalid values 
	uint32_t* graphNodeIndexMap = (uint32_t*)scratch; scratch = pointerOffset(scratch, align16(desc->chunkCount * sizeof(uint32_t)));
	memset(graphNodeIndexMap, 0xFF, desc->chunkCount*sizeof(uint32_t));

	// Fill graphNodeIndexMap
	uint32_t graphNodeCount = 0;
	for (uint32_t i = 0; i < desc->chunkCount; ++i)
	{
		if ((chunkAnnotation[i] & ChunkAnnotation::Support) != 0)
		{
			graphNodeIndexMap[i] = graphNodeCount++;
		}
	}
	NVBLAST_ASSERT(graphNodeCount == supportChunkCount);

	// Scratch array for bond sorting, of size 2*desc->bondCount
	BondSortData* bondSortArray = (BondSortData*)scratch; scratch = pointerOffset(scratch, align16(2 * desc->bondCount*sizeof(BondSortData)));

	// Bond remapping array of size desc->bondCount
	uint32_t* bondMap = (uint32_t*)scratch;
	memset(bondMap, 0xFF, desc->bondCount*sizeof(uint32_t));

	// Eliminate bad or redundant bonds, finding actual bond count
	uint32_t bondCount = 0;
	if (desc->bondCount > 0)
	{
		// Check for duplicates from input data as well as non-support chunk indices.  All such bonds must be removed.
		bool invalidFound = false;
		bool duplicateFound = false;
		bool nonSupportFound = false;

		// Construct temp array of chunk index pairs and bond indices.  This array is symmetrized to hold the reversed chunk indices as well.
		uint32_t bondSortArraySize = 0;
		BondSortData* t = bondSortArray;
		bool addWorldNode = false;
		for (uint32_t i = 0; i < desc->bondCount; ++i)
		{
			const NvBlastBondDesc& bondDesc = desc->bondDescs[i];
			const uint32_t chunkIndex0 = bondDesc.chunkIndices[0];
			const uint32_t chunkIndex1 = bondDesc.chunkIndices[1];

			if ((chunkIndex0 >= desc->chunkCount && !isInvalidIndex(chunkIndex0)) ||
				(chunkIndex1 >= desc->chunkCount && !isInvalidIndex(chunkIndex1)) ||
				chunkIndex0 == chunkIndex1)
			{
				invalidFound = true;
				continue;
			}

			uint32_t graphIndex0;
			if (!isInvalidIndex(chunkIndex0))
			{
				graphIndex0 = graphNodeIndexMap[chunkIndex0];
			}
			else
			{
				addWorldNode = true;
				graphIndex0 = graphNodeCount;	// Will set graphNodeCount = supportChunkCount + 1
			}

			uint32_t graphIndex1;
			if (!isInvalidIndex(chunkIndex1))
			{
				graphIndex1 = graphNodeIndexMap[chunkIndex1];
			}
			else
			{
				addWorldNode = true;
				graphIndex1 = graphNodeCount;	// Will set graphNodeCount = supportChunkCount + 1
			}

			if (isInvalidIndex(graphIndex0) || isInvalidIndex(graphIndex1))
			{
				nonSupportFound = true;
				continue;
			}

			t[bondSortArraySize++] = BondSortData(graphIndex0, graphIndex1, i);
			t[bondSortArraySize++] = BondSortData(graphIndex1, graphIndex0, i);
		}

		// Sort the temp array
		std::sort(bondSortArray, bondSortArray + bondSortArraySize, BondsOrdered());

		uint32_t symmetrizedBondCount = 0;
		for (uint32_t i = 0; i < bondSortArraySize; ++i)
		{
			const bool duplicate = i > 0 && bondSortArray[i].m_c0 == bondSortArray[i - 1].m_c0 && bondSortArray[i].m_c1 == bondSortArray[i - 1].m_c1;	// Since the array is sorted, uniqueness may be tested by only considering the previous element
			duplicateFound = duplicateFound || duplicate;
			if (!duplicate)
			{	// Keep this bond
				if (symmetrizedBondCount != i)
				{
					bondSortArray[symmetrizedBondCount] = bondSortArray[i];	// Compact array if we've dropped bonds
				}
				++symmetrizedBondCount;
			}
		}
		NVBLAST_ASSERT((symmetrizedBondCount & 1) == 0);	// Because we symmetrized, there should be an even number

		bondCount = symmetrizedBondCount / 2;

		// World node references found in bonds; add a world node
		if (addWorldNode)
		{
			++graphNodeCount;
		}

		// Report warnings
		if (invalidFound)
		{
			NVBLASTLL_LOG_WARNING(logFn, "NvBlastCreateAsset: Invalid bonds found (non-existent or same chunks referenced) and removed from asset.");
		}
		if (duplicateFound)
		{
			NVBLASTLL_LOG_WARNING(logFn, "NvBlastCreateAsset: Duplicate bonds found and removed from asset.");
		}
		if (nonSupportFound)
		{
			NVBLASTLL_LOG_WARNING(logFn, "NvBlastCreateAsset: Bonds referencing non-support chunks found and removed from asset.");
		}
	}

	// Allocate memory for asset
	NvBlastID id;
	memset(&id, 0, sizeof(NvBlastID));	// To do - create an actual id
	Asset* asset = initializeAsset(mem, id, desc->chunkCount, graphNodeCount, leafChunkCount, firstSubsupportChunkIndex, bondCount, logFn);

	// Asset data pointers
	SupportGraph& graph = asset->m_graph;
	NvBlastChunk* chunks = asset->getChunks();
	NvBlastBond* bonds = asset->getBonds();
	uint32_t* subtreeLeafChunkCounts = asset->getSubtreeLeafChunkCounts();

	// Create chunks
	uint32_t* graphChunkIndices = graph.getChunkIndices();
	memset(graphChunkIndices, 0xFF, graphNodeCount * sizeof(uint32_t));	// Ensures unmapped node indices go to invalidIndex - this is important for the world node, if added
	for (uint32_t i = 0; i < desc->chunkCount; ++i)
	{
		const NvBlastChunkDesc& chunkDesc = desc->chunkDescs[i];
		NvBlastChunk& assetChunk = chunks[i];
		memcpy(assetChunk.centroid, chunkDesc.centroid, 3 * sizeof(float));
		assetChunk.volume = chunkDesc.volume;
		assetChunk.parentChunkIndex = isInvalidIndex(chunkDesc.parentChunkDescIndex) ? chunkDesc.parentChunkDescIndex : chunkDesc.parentChunkDescIndex;
		assetChunk.firstChildIndex = invalidIndex<uint32_t>();	// Will be filled in below
		assetChunk.childIndexStop = assetChunk.firstChildIndex;
		assetChunk.userData = chunkDesc.userData;
		const uint32_t graphNodeIndex = graphNodeIndexMap[i];
		if (!isInvalidIndex(graphNodeIndex))
		{
			graphChunkIndices[graphNodeIndex] = i;
		}
	}

	// Copy chunkToGraphNodeMap
	memcpy(asset->getChunkToGraphNodeMap(), graphNodeIndexMap, desc->chunkCount * sizeof(uint32_t));

	// Count chunk children
	for (uint32_t i = 0; i < desc->chunkCount; ++i)
	{
		const uint32_t parentChunkIndex = chunks[i].parentChunkIndex;
		if (!isInvalidIndex(parentChunkIndex))
		{
			if (chunks[parentChunkIndex].childIndexStop == chunks[parentChunkIndex].firstChildIndex)
			{
				chunks[parentChunkIndex].childIndexStop = chunks[parentChunkIndex].firstChildIndex = i;
			}
			++chunks[parentChunkIndex].childIndexStop;
		}
	}

	// Create bonds
	uint32_t* graphAdjacencyPartition = graph.getAdjacencyPartition();
	uint32_t* graphAdjacentNodeIndices = graph.getAdjacentNodeIndices();
	uint32_t* graphAdjacentBondIndices = graph.getAdjacentBondIndices();
	if (bondCount > 0)
	{
		// Create the lookup table from the sorted array
		createIndexStartLookup<uint32_t>(graphAdjacencyPartition, 0, graphNodeCount - 1, &bondSortArray->m_c0, 2 * bondCount, sizeof(BondSortData));

		// Write the adjacent chunk and bond index data
		uint32_t bondIndex = 0;
		for (uint32_t i = 0; i < 2 * bondCount; ++i)
		{
			const BondSortData& bondSortData = bondSortArray[i];
			graphAdjacentNodeIndices[i] = bondSortData.m_c1;
			const uint32_t oldBondIndex = bondSortData.m_b;
			const NvBlastBondDesc& bondDesc = desc->bondDescs[oldBondIndex];
			if (isInvalidIndex(bondMap[oldBondIndex]))
			{
				bonds[bondIndex] = bondDesc.bond;
				bondMap[oldBondIndex] = bondIndex++;
			}
			NVBLAST_ASSERT(bondMap[oldBondIndex] < bondCount);
			graphAdjacentBondIndices[i] = bondMap[oldBondIndex];
		}
	}
	else
	{
		// No bonds - zero out all partition elements (including last one, to give zero size for adjacent data arrays)
		memset(graphAdjacencyPartition, 0, (graphNodeCount + 1)*sizeof(uint32_t));
	}

	// Count subtree leaf chunks
	memset(subtreeLeafChunkCounts, 0, desc->chunkCount*sizeof(uint32_t));
	uint32_t* breadthFirstChunkIndices = graphNodeIndexMap;	// Reusing graphNodeIndexMap ... graphNodeIndexMap may no longer be used
	for (uint32_t startChunkIndex = 0; startChunkIndex < desc->chunkCount; ++startChunkIndex)
	{
		if (!isInvalidIndex(chunks[startChunkIndex].parentChunkIndex))
		{
			break;	// Only iterate through root chunks at this level
		}
		const uint32_t enumeratedChunkCount = enumerateChunkHierarchyBreadthFirst(breadthFirstChunkIndices, desc->chunkCount, chunks, startChunkIndex);
		for (uint32_t chunkNum = enumeratedChunkCount; chunkNum--;)
		{
			const uint32_t chunkIndex = breadthFirstChunkIndices[chunkNum];
			const NvBlastChunk& chunk = chunks[chunkIndex];
			if (chunk.childIndexStop <= chunk.firstChildIndex)
			{
				subtreeLeafChunkCounts[chunkIndex] = 1;
			}
			if (!isInvalidIndex(chunk.parentChunkIndex))
			{
				subtreeLeafChunkCounts[chunk.parentChunkIndex] += subtreeLeafChunkCounts[chunkIndex];
			}
		}
	}

	return asset;
}


bool Asset::ensureExactSupportCoverage(uint32_t& supportChunkCount, uint32_t& leafChunkCount, char* chunkAnnotation, uint32_t chunkCount, NvBlastChunkDesc* chunkDescs, bool testOnly, NvBlastLog logFn)
{
	// Clear leafChunkCount
	leafChunkCount = 0;

	memset(chunkAnnotation, 0, chunkCount);

	// Walk up the hierarchy from all chunks and mark all parents
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		if (chunkAnnotation[i] & Asset::ChunkAnnotation::Parent)
		{
			continue;
		}
		uint32_t chunkIndex = i;
		while (!isInvalidIndex(chunkIndex = chunkDescs[chunkIndex].parentChunkDescIndex))
		{
			chunkAnnotation[chunkIndex] = Asset::ChunkAnnotation::Parent;	// Note as non-leaf
		}
	}

	// Walk up the hierarchy from all leaves (counting them with leafChunkCount) and keep track of the support chunks found on each chain
	// Exactly one support chunk should be found on each walk.  Remove all but the highest support markings if more than one are found.
	bool redundantCoverage = false;
	bool insufficientCoverage = false;
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		if (chunkAnnotation[i] & Asset::ChunkAnnotation::Parent)
		{
			continue;
		}
		++leafChunkCount;
		uint32_t supportChunkIndex;
		supportChunkIndex = invalidIndex<uint32_t>();
		uint32_t chunkIndex = i;
		bool doneWithChain = false;
		do
		{
			if (chunkDescs[chunkIndex].flags & NvBlastChunkDesc::SupportFlag)
			{
				if (chunkAnnotation[chunkIndex] & Asset::ChunkAnnotation::Support)
				{
					// We've already been up this chain and marked this as support, so we have unique coverage already
					doneWithChain = true;
				}
				chunkAnnotation[chunkIndex] |= Asset::ChunkAnnotation::Support;	// Note as support
				if (!isInvalidIndex(supportChunkIndex))
				{
					if (testOnly)
					{
						return false;
					}
					redundantCoverage = true;
					chunkAnnotation[supportChunkIndex] &= ~Asset::ChunkAnnotation::Support;	// Remove support marking
					do	// Run up the hierarchy from supportChunkIndex to chunkIndex and remove the supersupport markings
					{
						supportChunkIndex = chunkDescs[supportChunkIndex].parentChunkDescIndex;
						chunkAnnotation[supportChunkIndex] &= ~Asset::ChunkAnnotation::SuperSupport;	// Remove supersupport marking
					} while (supportChunkIndex != chunkIndex);
				}
				supportChunkIndex = chunkIndex;
			}
			else
			if (!isInvalidIndex(supportChunkIndex))
			{
				chunkAnnotation[chunkIndex] |= Asset::ChunkAnnotation::SuperSupport;	// Not a support chunk and we've already found a support chunk, so this is super-support
			}
		} while (!doneWithChain && !isInvalidIndex(chunkIndex = chunkDescs[chunkIndex].parentChunkDescIndex));
		if (isInvalidIndex(supportChunkIndex))
		{
			if (testOnly)
			{
				return false;
			}
			insufficientCoverage = true;
		}
	}

	if (redundantCoverage)
	{
		NVBLASTLL_LOG_INFO(logFn, "NvBlastCreateAsset: some leaf-to-root chains had more than one support chunk.  Some support chunks removed.");
	}

	if (insufficientCoverage)
	{
		// If coverage was insufficient, then walk up the hierarchy again and mark all chunks that have a support descendant.
		// This will allow us to place support chunks at the highest possible level to obtain coverage.
		for (uint32_t i = 0; i < chunkCount; ++i)
		{
			if (chunkAnnotation[i] & Asset::ChunkAnnotation::Parent)
			{
				continue;
			}
			bool supportFound = false;
			uint32_t chunkIndex = i;
			do
			{
				if (chunkAnnotation[chunkIndex] & Asset::ChunkAnnotation::Support)
				{
					supportFound = true;
				}
				else
				if (supportFound)
				{
					chunkAnnotation[chunkIndex] |= Asset::ChunkAnnotation::SuperSupport;	// Note that a descendant has support
				}
			} while (!isInvalidIndex(chunkIndex = chunkDescs[chunkIndex].parentChunkDescIndex));
		}

		// Now walk up the hierarchy from each leaf one more time, and make sure there is coverage
		for (uint32_t i = 0; i < chunkCount; ++i)
		{
			if (chunkAnnotation[i] & Asset::ChunkAnnotation::Parent)
			{
				continue;
			}
			uint32_t previousChunkIndex;
			previousChunkIndex = invalidIndex<uint32_t>();
			uint32_t chunkIndex = i;
			for (;;)
			{
				if (chunkAnnotation[chunkIndex] & Asset::ChunkAnnotation::Support)
				{
					break;	// There is support along this chain
				}
				if (chunkAnnotation[chunkIndex] & Asset::ChunkAnnotation::SuperSupport)
				{
					NVBLAST_ASSERT(!isInvalidIndex(previousChunkIndex));	// This should be impossible
					chunkAnnotation[previousChunkIndex] |= Asset::ChunkAnnotation::Support;	// There is no support along this chain, and this is the highest place where we can put support
					break;
				}
				previousChunkIndex = chunkIndex;
				chunkIndex = chunkDescs[chunkIndex].parentChunkDescIndex;
				if (isInvalidIndex(chunkIndex))
				{
					chunkAnnotation[previousChunkIndex] |= Asset::ChunkAnnotation::Support;	// There was no support found anywhere in the hierarchy, so we add it at the root
					break;
				}
			}
		}

		NVBLASTLL_LOG_INFO(logFn, "NvBlastCreateAsset: some leaf-to-root chains had no support chunks.  Support chunks added.");
	}

	// Apply changes and count the number of support chunks
	supportChunkCount = 0;
	for (uint32_t i = 0; i < chunkCount; ++i)
	{
		const bool wasSupport = (chunkDescs[i].flags & NvBlastChunkDesc::SupportFlag) != 0;
		const bool nowSupport = (chunkAnnotation[i] & Asset::ChunkAnnotation::Support) != 0;
		if (wasSupport != nowSupport)
		{
			chunkDescs[i].flags ^= NvBlastChunkDesc::SupportFlag;
		}
		if ((chunkDescs[i].flags & NvBlastChunkDesc::SupportFlag) != 0)
		{
			++supportChunkCount;
		}
	}

	return !redundantCoverage && !insufficientCoverage;
}


bool Asset::testForValidChunkOrder(uint32_t chunkCount, const NvBlastChunkDesc* chunkDescs, const char* chunkAnnotation, void* scratch)
{
    char* chunkMarks = static_cast<char*>(memset(scratch, 0, chunkCount));

    uint32_t currentParentChunkIndex = invalidIndex<uint32_t>();
    for (uint32_t i = 0; i < chunkCount; ++i)
    {
        const uint32_t parentChunkIndex = chunkDescs[i].parentChunkDescIndex;

        if (!isInvalidIndex(parentChunkIndex) && parentChunkIndex >= i) // 'chunks should come after their parents'
        {
            return false;
        }

        if (parentChunkIndex != currentParentChunkIndex)
        {
            if (!isInvalidIndex(currentParentChunkIndex))
            {
                chunkMarks[currentParentChunkIndex] = 1;
            }
            currentParentChunkIndex = parentChunkIndex;
            if (isInvalidIndex(currentParentChunkIndex))    // 'root chunks should go first'
            {
                return false;
            }
            else if (chunkMarks[currentParentChunkIndex] != 0)  // 'all chunks with same parent index should go in a row'
            {
                return false;
            }
        }

        if (i < chunkCount - 1)
        {
            const bool upperSupport0 = (chunkAnnotation[i] & ChunkAnnotation::UpperSupport) != 0;
            const bool upperSupport1 = (chunkAnnotation[i + 1] & ChunkAnnotation::UpperSupport) != 0;

            if (!upperSupport0 && upperSupport1)    // 'upper-support chunks should come before subsupport chunks'
            {
                return false;
            }
        }
    }

    return true;
}

} // namespace Blast
} // namespace Nv


// API implementation

extern "C"
{

size_t NvBlastGetRequiredScratchForCreateAsset(const NvBlastAssetDesc* desc, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(desc != nullptr, logFn, "NvBlastGetRequiredScratchForCreateAsset: NULL desc pointer input.", return 0);

	return Nv::Blast::Asset::createRequiredScratch(desc);
}


size_t NvBlastGetAssetMemorySize(const NvBlastAssetDesc* desc, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(desc != nullptr, logFn, "NvBlastGetAssetMemorySize: NULL desc input.", return 0);

	return Nv::Blast::Asset::getMemorySize(desc);
}


NvBlastAsset* NvBlastCreateAsset(void* mem, const NvBlastAssetDesc* desc, void* scratch, NvBlastLog logFn)
{
	return Nv::Blast::Asset::create(mem, desc, scratch, logFn);
}


size_t NvBlastAssetGetFamilyMemorySize(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetFamilyMemorySize: NULL asset pointer input.", return 0);

	return Nv::Blast::getFamilyMemorySize(reinterpret_cast<const Nv::Blast::Asset*>(asset));
}


NvBlastID NvBlastAssetGetID(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetID: NULL asset pointer input.", NvBlastID zero; memset(&zero, 0, sizeof(NvBlastID)); return zero);

	return ((Nv::Blast::Asset*)asset)->m_ID;
}


bool NvBlastAssetSetID(NvBlastAsset* asset, const NvBlastID* id, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetSetID: NULL asset pointer input.", return false);
	NVBLASTLL_CHECK(id != nullptr, logFn, "NvBlastAssetSetID: NULL id pointer input.", return false);

	((Nv::Blast::Asset*)asset)->m_ID = *id;

	return true;
}


uint32_t NvBlastAssetGetFormatVersion(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetFormatVersion: NULL asset input.", return UINT32_MAX);

	return ((Nv::Blast::Asset*)asset)->m_header.formatVersion;
}


uint32_t NvBlastAssetGetSize(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetSize: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->m_header.size;
}


uint32_t NvBlastAssetGetChunkCount(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetChunkCount: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->m_chunkCount;
}


uint32_t NvBlastAssetGetSupportChunkCount(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetSupportChunkCount: NULL asset input.", return 0);

	const Nv::Blast::Asset* a = reinterpret_cast<const Nv::Blast::Asset*>(asset);
	const Nv::Blast::SupportGraph& graph = a->m_graph;

	if (graph.m_nodeCount == 0)
	{
		return 0;	// This shouldn't happen
	}

	return Nv::Blast::isInvalidIndex(graph.getChunkIndices()[graph.m_nodeCount - 1]) ? graph.m_nodeCount - 1 : graph.m_nodeCount;
}


uint32_t NvBlastAssetGetLeafChunkCount(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetLeafChunkCount: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->m_leafChunkCount;
}


uint32_t NvBlastAssetGetFirstSubsupportChunkIndex(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetFirstSubsupportChunkIndex: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->m_firstSubsupportChunkIndex;
}


uint32_t NvBlastAssetGetBondCount(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetBondCount: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->m_bondCount;
}


const NvBlastSupportGraph NvBlastAssetGetSupportGraph(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetSupportGraph: NULL asset input.",
		NvBlastSupportGraph blank; blank.nodeCount = 0; blank.chunkIndices = blank.adjacencyPartition = blank.adjacentNodeIndices = blank.adjacentBondIndices = nullptr; return blank);

	const Nv::Blast::SupportGraph& supportGraph = static_cast<const Nv::Blast::Asset*>(asset)->m_graph;

	NvBlastSupportGraph graph;
	graph.nodeCount = supportGraph.m_nodeCount;
	graph.chunkIndices = supportGraph.getChunkIndices();
	graph.adjacencyPartition = supportGraph.getAdjacencyPartition();
	graph.adjacentNodeIndices = supportGraph.getAdjacentNodeIndices();
	graph.adjacentBondIndices = supportGraph.getAdjacentBondIndices();

	return graph;
}


const uint32_t* NvBlastAssetGetChunkToGraphNodeMap(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetChunkToGraphNodeMap: NULL asset input.", return nullptr);

	return static_cast<const Nv::Blast::Asset*>(asset)->getChunkToGraphNodeMap();
}


const NvBlastChunk* NvBlastAssetGetChunks(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetChunks: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->getChunks();
}


const NvBlastBond* NvBlastAssetGetBonds(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetBonds: NULL asset input.", return 0);

	return ((Nv::Blast::Asset*)asset)->getBonds();
}


uint32_t NvBlastAssetGetActorSerializationSizeUpperBound(const NvBlastAsset* asset, NvBlastLog logFn)
{
	NVBLASTLL_CHECK(asset != nullptr, logFn, "NvBlastAssetGetActorSerializationSizeUpperBound: NULL asset input.", return 0);

	const Nv::Blast::Asset& solverAsset = *(const Nv::Blast::Asset*)asset;
	const uint32_t graphNodeCount = solverAsset.m_graph.m_nodeCount;

	// Calculate serialization size for an actor with all graph nodes (and therefore all bonds), and somehow with all graph nodes visible (after all, this is an upper bound).
	const uint64_t upperBound = Nv::Blast::getActorSerializationSize(graphNodeCount, solverAsset.getLowerSupportChunkCount(), graphNodeCount, solverAsset.getBondCount());

	if (upperBound > UINT32_MAX)
	{
		NVBLASTLL_LOG_WARNING(logFn, "NvBlastAssetGetActorSerializationSizeUpperBound: Serialization block size exceeds 4GB.  Returning 0.\n");
		return 0;
	}

	return static_cast<uint32_t>(upperBound);
}

} // extern "C"
