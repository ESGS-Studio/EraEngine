// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#include "rendering/light_probe.h"
#include "rendering/render_utils.h"
#include "rendering/render_resources.h"
#include "rendering/pbr_raytracer.h"

#include "core/imgui.h"

#include "dx/dx_pipeline.h"
#include "dx/dx_command_list.h"
#include "dx/dx_context.h"
#include "dx/dx_barrier_batcher.h"
#include "dx/dx_profiling.h"

#include "geometry/mesh_builder.h"

#include "light_probe_rs.hlsli"
#include "transform.hlsli"

namespace era_engine
{
	static dx_pipeline visualizeGridPipeline;
	static dx_pipeline visualizeRaysPipeline;

	static dx_pipeline probeUpdateIrradiancePipeline;
	static dx_pipeline probeUpdateDepthPipeline;

	static dx_pipeline testSamplePipeline;

	static dx_mesh sphereMesh;
	static submesh_info sphereSubmesh;

	static const DXGI_FORMAT irradianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;// DXGI_FORMAT_R11G11B10_FLOAT;
	static const DXGI_FORMAT depthFormat = DXGI_FORMAT_R16G16_FLOAT;
	static const DXGI_FORMAT raytracedRadianceFormat = DXGI_FORMAT_R11G11B10_FLOAT;
	static const DXGI_FORMAT raytracedDirectionAndDistanceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	struct light_probe_tracer : pbr_raytracer
	{
		void initialize();

		void render(dx_command_list* cl, const raytracing_tlas& tlas,
			const light_probe_grid& grid,
			const common_render_data& common);

	private:
		const uint32 maxRecursionDepth = 2;
		const uint32 maxPayloadSize = 4 * sizeof(float); // Radiance-payload is 1 x float3, 1 x float

		// Only descriptors in here!
		struct input_resources
		{
			dx_cpu_descriptor_handle tlas;
			dx_cpu_descriptor_handle sky;
			dx_cpu_descriptor_handle irradiance;
			dx_cpu_descriptor_handle depth;
		};

		struct output_resources
		{
			dx_cpu_descriptor_handle radiance;
			dx_cpu_descriptor_handle directionAndDistance;
		};
	};

	static light_probe_tracer lightProbeTracer;

	void initializeLightProbePipelines()
	{
		if (!dxContext.featureSupport.raytracing())
			return;

		{
			auto& desc = CREATE_GRAPHICS_PIPELINE
				.inputLayout(inputLayout_position)
				.renderTargets(hdrFormat, depthStencilFormat);

			visualizeGridPipeline = createReloadablePipeline(desc, { "light_probe_grid_visualization_vs", "light_probe_grid_visualization_ps" });
		}

		{
			auto& desc = CREATE_GRAPHICS_PIPELINE
				.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
				.renderTargets(hdrFormat, depthStencilFormat);

			visualizeRaysPipeline = createReloadablePipeline(desc, { "light_probe_ray_visualization_vs", "light_probe_ray_visualization_ps" });
		}

		{
			auto& desc = CREATE_GRAPHICS_PIPELINE
				.inputLayout(inputLayout_position)
				.renderTargets(hdrFormat, depthStencilFormat);

			testSamplePipeline = createReloadablePipeline(desc, { "light_probe_test_sample_vs", "light_probe_test_sample_ps" });
		}

		mesh_builder builder(mesh_creation_flags_with_positions);
		builder.pushSphere({});
		sphereSubmesh = builder.endSubmesh();
		sphereMesh = builder.createDXMesh();

		probeUpdateIrradiancePipeline = createReloadablePipeline("light_probe_update_irradiance_cs");
		probeUpdateDepthPipeline = createReloadablePipeline("light_probe_update_depth_cs");

		lightProbeTracer.initialize();
	}

	void light_probe_grid::initialize(vec3 minCorner, vec3 dimensions, float cellSize)
	{
		if (!dxContext.featureSupport.raytracing())
			return;

		uint32 gridSizeX = (uint32)(ceilf(dimensions.x / cellSize));
		uint32 gridSizeY = (uint32)(ceilf(dimensions.y / cellSize));
		uint32 gridSizeZ = (uint32)(ceilf(dimensions.z / cellSize));

		vec3 oldDimensions = dimensions;

		dimensions = vec3((float)gridSizeX, (float)gridSizeY, (float)gridSizeZ) * cellSize;
		minCorner -= (dimensions - oldDimensions) * 0.5f;

		this->minCorner = minCorner;
		this->cellSize = cellSize;

		this->numNodesX = gridSizeX + 1;
		this->numNodesY = gridSizeY + 1;
		this->numNodesZ = gridSizeZ + 1;
		this->totalNumNodes = numNodesX * numNodesY * numNodesZ;

		uint32 irradianceYSliceWidth = numNodesX * LIGHT_PROBE_TOTAL_RESOLUTION;
		uint32 irradianceTexWidth = numNodesY * irradianceYSliceWidth;
		uint32 irradianceTexHeight = numNodesZ * LIGHT_PROBE_TOTAL_RESOLUTION;

		uint32 depthYSliceWidth = numNodesX * LIGHT_PROBE_TOTAL_DEPTH_RESOLUTION;
		uint32 depthTexWidth = numNodesY * depthYSliceWidth;
		uint32 depthTexHeight = numNodesZ * LIGHT_PROBE_TOTAL_DEPTH_RESOLUTION;

#if 0
		struct color_rgba
		{
			uint8 r, g, b, a;
		};

		color_rgba* testBuffer = new color_rgba[irradianceTexWidth * irradianceTexHeight];


		color_rgba testProbe[LIGHT_PROBE_TOTAL_RESOLUTION][LIGHT_PROBE_TOTAL_RESOLUTION] = {};

		for (uint32 ly = 1; ly < LIGHT_PROBE_TOTAL_RESOLUTION - 1; ++ly)
		{
			for (uint32 lx = 1; lx < LIGHT_PROBE_TOTAL_RESOLUTION - 1; ++lx)
			{
				// Subtract the border.
				float x = (float)(lx - 1);
				float y = (float)(ly - 1);

				vec2 uv = (vec2(x, y) + 0.5f) / LIGHT_PROBE_RESOLUTION;
				vec2 oct = uv * 2.f - 1.f;

				vec3 dir = decodeOctahedral(oct);

				vec3 c = lerp(vec3(0.f, 0.f, 1.f), vec3(1.f, 0.f, 0.f), dir.x * 0.5f + 0.5f);
				//vec3 c = vec3(dir.x, 0.f, 0.f);
				testProbe[ly][lx] = { (uint8)(clamp01(c.x) * 255.f), (uint8)(clamp01(c.y) * 255.f), (uint8)(clamp01(c.z) * 255.f), 255 };
				int a = 0;
			}
		}

		// Copy border.
		testProbe[0][0] = testProbe[6][6];
		testProbe[0][7] = testProbe[6][1];
		testProbe[7][0] = testProbe[1][6];
		testProbe[7][7] = testProbe[1][1];

		testProbe[0][1] = testProbe[1][6];
		testProbe[0][2] = testProbe[1][5];
		testProbe[0][3] = testProbe[1][4];
		testProbe[0][4] = testProbe[1][3];
		testProbe[0][5] = testProbe[1][2];
		testProbe[0][6] = testProbe[1][1];

		testProbe[7][1] = testProbe[6][6];
		testProbe[7][2] = testProbe[6][5];
		testProbe[7][3] = testProbe[6][4];
		testProbe[7][4] = testProbe[6][3];
		testProbe[7][5] = testProbe[6][2];
		testProbe[7][6] = testProbe[6][1];

		testProbe[1][0] = testProbe[6][1];
		testProbe[2][0] = testProbe[5][1];
		testProbe[3][0] = testProbe[4][1];
		testProbe[4][0] = testProbe[3][1];
		testProbe[5][0] = testProbe[2][1];
		testProbe[6][0] = testProbe[1][1];

		testProbe[1][7] = testProbe[6][6];
		testProbe[2][7] = testProbe[5][6];
		testProbe[3][7] = testProbe[4][6];
		testProbe[4][7] = testProbe[3][6];
		testProbe[5][7] = testProbe[2][6];
		testProbe[6][7] = testProbe[1][6];

		for (uint32 z = 0; z < numNodesZ; ++z)
		{
			for (uint32 y = 0; y < numNodesY; ++y)
			{
				for (uint32 x = 0; x < numNodesX; ++x)
				{
					uint32 texStartX = irradianceYSliceWidth * y + x * LIGHT_PROBE_TOTAL_RESOLUTION;
					uint32 texStartY = z * LIGHT_PROBE_TOTAL_RESOLUTION;

					for (uint32 ly = 0; ly < LIGHT_PROBE_TOTAL_RESOLUTION; ++ly)
					{
						for (uint32 lx = 0; lx < LIGHT_PROBE_TOTAL_RESOLUTION; ++lx)
						{
							uint32 gx = texStartX + lx;
							uint32 gy = texStartY + ly;

							uint32 c = irradianceTexWidth * gy + gx;

							testBuffer[c] = testProbe[ly][lx];
						}
					}
				}
			}
		}

		this->irradiance = createTexture(testBuffer, irradianceTexWidth, irradianceTexHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);

		delete[] testBuffer;
#else
		this->irradiance = createTexture(0, irradianceTexWidth, irradianceTexHeight, irradianceFormat, false, false, true);
#endif

		this->depth = createTexture(0, depthTexWidth, depthTexHeight, depthFormat, false, false, true);

		this->raytracedRadiance = createTexture(0, NUM_RAYS_PER_PROBE, totalNumNodes, raytracedRadianceFormat, 0, 0, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		this->raytracedDirectionAndDistance = createTexture(0, NUM_RAYS_PER_PROBE, totalNumNodes, raytracedDirectionAndDistanceFormat, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	struct visualize_grid_render_data
	{
		mat4 transform;
		dx_vertex_buffer_group_view vertexBuffer;
		dx_index_buffer_view indexBuffer;
		submesh_info submesh;

		float cellSize;
		uint32 countX;
		uint32 countY;
		uint32 countZ;
		uint32 total;

		ref<dx_texture> texture0;
		ref<dx_texture> texture1;
	};

	struct visualize_grid_pipeline
	{
		PIPELINE_SETUP_DECL;
		PIPELINE_RENDER_DECL(visualize_grid_render_data);
	};

	PIPELINE_SETUP_IMPL(visualize_grid_pipeline)
	{
		cl->setPipelineState(*visualizeGridPipeline.pipeline);
		cl->setGraphicsRootSignature(*visualizeGridPipeline.rootSignature);
		cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	PIPELINE_RENDER_IMPL(visualize_grid_pipeline, visualize_grid_render_data)
	{
		vec2 uvScale = 1.f / vec2((float)(data.countX * data.countY), (float)data.countZ);
		cl->setGraphics32BitConstants(LIGHT_PROBE_GRID_VISUALIZATION_RS_CB, light_probe_grid_visualization_cb{ viewProj * data.transform, uvScale, data.cellSize, data.countX, data.countY });
		cl->setDescriptorHeapSRV(LIGHT_PROBE_GRID_VISUALIZATION_RS_IRRADIANCE, 0, data.texture0);

		cl->setVertexBuffer(0, data.vertexBuffer.positions);
		cl->setVertexBuffer(1, data.vertexBuffer.others);
		cl->setIndexBuffer(data.indexBuffer);
		cl->drawIndexed(data.submesh.numIndices, data.total, data.submesh.firstIndex, data.submesh.baseVertex, 0);
	}

	struct visualize_rays_render_data
	{
		mat4 transform;

		float cellSize;
		uint32 countX;
		uint32 countY;
		uint32 countZ;
		uint32 total;

		ref<dx_texture> texture0;
		ref<dx_texture> texture1;
	};

	struct visualize_rays_pipeline
	{
		PIPELINE_SETUP_DECL;
		PIPELINE_RENDER_DECL(visualize_rays_render_data);
	};

	PIPELINE_SETUP_IMPL(visualize_rays_pipeline)
	{
		cl->setPipelineState(*visualizeRaysPipeline.pipeline);
		cl->setGraphicsRootSignature(*visualizeRaysPipeline.rootSignature);
		cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	}

	PIPELINE_RENDER_IMPL(visualize_rays_pipeline, visualize_rays_render_data)
	{
		cl->setGraphics32BitConstants(LIGHT_PROBE_RAY_VISUALIZATION_RS_CB, light_probe_ray_visualization_cb{ viewProj * data.transform, data.cellSize, data.countX, data.countY });
		cl->setDescriptorHeapSRV(LIGHT_PROBE_RAY_VISUALIZATION_RS_RAYS, 0, data.texture0);
		cl->setDescriptorHeapSRV(LIGHT_PROBE_RAY_VISUALIZATION_RS_RAYS, 1, data.texture1);

		cl->transitionBarrier(data.texture0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		cl->transitionBarrier(data.texture1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		cl->draw(2, NUM_RAYS_PER_PROBE * data.total, 0, 0);
		cl->transitionBarrier(data.texture0, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cl->transitionBarrier(data.texture1, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	void light_probe_grid::visualize(opaque_render_pass* renderPass)
	{
		if (!dxContext.featureSupport.raytracing() || totalNumNodes == 0)
			return;

		if (visualizeProbes)
		{
			mat4 transform = create_translation_matrix(minCorner);
			visualize_grid_render_data data =
			{
				transform, sphereMesh.vertexBuffer, sphereMesh.indexBuffer, sphereSubmesh, cellSize, numNodesX, numNodesY, numNodesZ, totalNumNodes, irradiance
			};

			renderPass->renderObject<visualize_grid_pipeline>(data);
		}
		if (visualizeRays)
		{
			mat4 transform = create_translation_matrix(minCorner);
			visualize_rays_render_data data = { transform, cellSize, numNodesX, numNodesY, numNodesZ, totalNumNodes, raytracedRadiance, raytracedDirectionAndDistance };

			renderPass->renderObject<visualize_rays_pipeline>(data);
		}
	}

	void light_probe_grid::updateProbes(dx_command_list* cl, const raytracing_tlas& lightProbeTlas, const common_render_data& common) const
	{
		if (!dxContext.featureSupport.raytracing() || totalNumNodes == 0)
			return;

		if (autoRotateRays || rotateRays)
		{
			rayRotation = rng.randomRotation();
			rotateRays = false;
		}

		lightProbeTracer.finalizeForRender();
		lightProbeTracer.render(cl, lightProbeTlas, *this, common);
	}

#define LIGHT_PROBE_TRACING_RS_RESOURCES	0
#define LIGHT_PROBE_TRACING_RS_CB			1
#define LIGHT_PROBE_TRACING_RS_SUN			2

	void light_probe_tracer::initialize()
	{
		const wchar* shaderPath = L"shaders/light_probe/light_probe_trace_rts.hlsl";

		const uint32 numInputResources = sizeof(input_resources) / sizeof(dx_cpu_descriptor_handle);
		const uint32 numOutputResources = sizeof(output_resources) / sizeof(dx_cpu_descriptor_handle);

		CD3DX12_DESCRIPTOR_RANGE resourceRanges[] =
		{
			// Must be input first, then output
			CD3DX12_DESCRIPTOR_RANGE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, numInputResources, 0),
			CD3DX12_DESCRIPTOR_RANGE(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, numOutputResources, 0),
		};

		CD3DX12_ROOT_PARAMETER globalRootParameters[] =
		{
			root_descriptor_table(arraysize(resourceRanges), resourceRanges),
			root_constants<light_probe_trace_cb>(0),
			root_cbv(1),
		};

		CD3DX12_STATIC_SAMPLER_DESC globalStaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

		D3D12_ROOT_SIGNATURE_DESC globalDesc =
		{
			arraysize(globalRootParameters), globalRootParameters,
			1, &globalStaticSampler
		};

		pbr_raytracer::initialize(shaderPath, maxPayloadSize, maxRecursionDepth, globalDesc);

		allocateDescriptorHeapSpaceForGlobalResources<input_resources, output_resources>(descriptorHeap);
	}

	void light_probe_tracer::render(dx_command_list* cl, const raytracing_tlas& tlas, const light_probe_grid& grid, const common_render_data& common)
	{
		if (!tlas.tlas || grid.totalNumNodes == 0)
			return;

		{
			PROFILE_ALL(cl, "Raytrace probes");

			input_resources in;
			in.tlas = tlas.tlas->raytracingSRV;
			in.sky = common.sky->defaultSRV;
			in.irradiance = grid.irradiance->defaultSRV;
			in.depth = grid.depth->defaultSRV;

			output_resources out;
			out.radiance = grid.raytracedRadiance->defaultUAV;
			out.directionAndDistance = grid.raytracedDirectionAndDistance->defaultUAV;

			dx_gpu_descriptor_handle gpuHandle = copyGlobalResourcesToDescriptorHeap(in, out);

			// Fill out description
			D3D12_DISPATCH_RAYS_DESC raytraceDesc;
			fillOutRayTracingRenderDesc(bindingTable.getBuffer(), raytraceDesc,
				NUM_RAYS_PER_PROBE, grid.totalNumNodes, 1,
				numRayTypes, bindingTable.getNumberOfHitGroups());

			// Set up pipeline
			cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptorHeap.descriptorHeap);

			cl->setPipelineState(pipeline.pipeline);
			cl->setComputeRootSignature(pipeline.rootSignature);

			light_probe_trace_cb cb;
			cb.rayRotation = create_model_matrix(0.f, grid.rayRotation);
			cb.grid = grid.getCB();
			cb.sampleSkyFromTexture = !common.proceduralSky;

			cl->setComputeDescriptorTable(LIGHT_PROBE_TRACING_RS_RESOURCES, gpuHandle);
			cl->setCompute32BitConstants(LIGHT_PROBE_TRACING_RS_CB, cb);
			cl->setComputeDynamicConstantBuffer(LIGHT_PROBE_TRACING_RS_SUN, common.lightingCBV);

			cl->raytrace(raytraceDesc);

			cl->resetToDynamicDescriptorHeap();
		}

		barrier_batcher(cl)
			.transition(grid.raytracedRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			.transition(grid.raytracedDirectionAndDistance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			.transition(grid.irradiance, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			.transition(grid.depth, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		{
			PROFILE_ALL(cl, "Update irradiance");

			cl->setPipelineState(*probeUpdateIrradiancePipeline.pipeline);
			cl->setComputeRootSignature(*probeUpdateIrradiancePipeline.rootSignature);

			light_probe_update_cb cb;
			cb.countX = grid.numNodesX;
			cb.countY = grid.numNodesY;

			cl->setCompute32BitConstants(LIGHT_PROBE_UPDATE_RS_CB, cb);
			cl->setDescriptorHeapSRV(LIGHT_PROBE_UPDATE_RS_RAYTRACE_RESULTS, 0, grid.raytracedRadiance);
			cl->setDescriptorHeapSRV(LIGHT_PROBE_UPDATE_RS_RAYTRACE_RESULTS, 1, grid.raytracedDirectionAndDistance);
			cl->setDescriptorHeapUAV(LIGHT_PROBE_UPDATE_RS_RAYTRACE_RESULTS, 2, grid.irradiance);

			cl->dispatch(bucketize(grid.irradiance->width, LIGHT_PROBE_BLOCK_SIZE), bucketize(grid.irradiance->height, LIGHT_PROBE_BLOCK_SIZE));
		}

#if 0
		{
			PROFILE_ALL(cl, "Update depth");

			cl->setPipelineState(*probeUpdateDepthPipeline.pipeline);
			cl->setComputeRootSignature(*probeUpdateDepthPipeline.rootSignature);

			light_probe_update_cb cb;
			cb.countX = grid.numNodesX;
			cb.countY = grid.numNodesY;

			cl->setCompute32BitConstants(LIGHT_PROBE_UPDATE_RS_CB, cb);
			cl->setDescriptorHeapSRV(LIGHT_PROBE_UPDATE_RS_RAYTRACE_RESULTS, 0, grid.raytracedRadiance);
			cl->setDescriptorHeapSRV(LIGHT_PROBE_UPDATE_RS_RAYTRACE_RESULTS, 1, grid.raytracedDirectionAndDistance);
			cl->setDescriptorHeapUAV(LIGHT_PROBE_UPDATE_RS_RAYTRACE_RESULTS, 2, grid.depth);

			cl->dispatch(bucketize(grid.depth->width, LIGHT_PROBE_BLOCK_SIZE), bucketize(grid.depth->height, LIGHT_PROBE_BLOCK_SIZE));
		}
#endif

		barrier_batcher(cl)
			.transition(grid.raytracedRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			.transition(grid.raytracedDirectionAndDistance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			.transition(grid.irradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON)
			.transition(grid.depth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
	}
}