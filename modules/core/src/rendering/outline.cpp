// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#include "rendering/outline.h"
#include "rendering/render_pass.h"
#include "rendering/material.h"
#include "rendering/render_resources.h"
#include "rendering/render_algorithms.h"

#include "dx/dx_pipeline.h"

#include "outline_rs.hlsli"

namespace era_engine
{
	static dx_pipeline outlinePipeline;

	void initializeOutlinePipelines()
	{
		auto& markerDesc = CREATE_GRAPHICS_PIPELINE
			.inputLayout(inputLayout_position)
			.renderTargets(0, 0, depthStencilFormat)
			.stencilSettings(D3D12_COMPARISON_FUNC_ALWAYS,
				D3D12_STENCIL_OP_REPLACE,
				D3D12_STENCIL_OP_REPLACE,
				D3D12_STENCIL_OP_KEEP,
				D3D12_DEFAULT_STENCIL_READ_MASK,
				stencil_flag_selected_object) // Mark selected object
			.depthSettings(false, false)
			.cullingOff(); // Since this is fairly light-weight, we only render double sided

		outlinePipeline = createReloadablePipeline(markerDesc, { "outline_vs" }, rs_in_vertex_shader);
	}

	struct outline_render_data
	{
		mat4 transform;
		dx_vertex_buffer_view vertexBuffer;
		dx_index_buffer_view indexBuffer;
		submesh_info submesh;
	};

	struct outline_pipeline
	{
		PIPELINE_SETUP_DECL
		{
			cl->setPipelineState(*outlinePipeline.pipeline);
			cl->setGraphicsRootSignature(*outlinePipeline.rootSignature);

			cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		}

			PIPELINE_RENDER_DECL(outline_render_data)
		{
			cl->setGraphics32BitConstants(OUTLINE_RS_MVP, outline_marker_cb{ viewProj * data.transform });

			cl->setVertexBuffer(0, data.vertexBuffer);
			cl->setIndexBuffer(data.indexBuffer);
			cl->drawIndexed(data.submesh.numIndices, 1, data.submesh.firstIndex, data.submesh.baseVertex, 0);
		}
	};

	void renderOutline(ldr_render_pass* renderPass, const mat4& transform, dx_vertex_buffer_view vertexBuffer, dx_index_buffer_view indexBuffer, submesh_info submesh)
	{
		outline_render_data data = {
			transform,
			vertexBuffer,
			indexBuffer,
			submesh,
		};

		renderPass->renderOutline<outline_pipeline>(data);
	}

	void renderOutline(ldr_render_pass* renderPass, const mat4& transform, dx_vertex_buffer_group_view vertexBuffer, dx_index_buffer_view indexBuffer, submesh_info submesh)
	{
		renderOutline(renderPass, transform, vertexBuffer.positions, indexBuffer, submesh);
	}
}