#include "renderman.h"
#include "contentman.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxil.lib")

namespace strikers {
result<> shaderman::compile_shader(
	const stringview& filepath, 
	const stringview& entrypoint,
	const stringview& target,
	dxdevice& device)
{
	using restype = result<>;

	static const wchar_t* k_program_name = L"graph";

	// load the file data
	uint32 codepage = 0;
	IDxcBlobEncoding* source_blob;
	HRESULT hres = m_utils->LoadFile(to_wstring(filepath).c_str(), &codepage, &source_blob);
	if (!SUCCEEDED(hres))
	{
		return restype::make_fail("failed loading file at filepath!");
	}
	DxcBuffer file_dxc_buffer = blob_encoding_to_dxc(source_blob);

	const shader_key key = make_shader_key(filepath, entrypoint, target);
	shader_entry& result_entry = m_shaders[key];
	// compile root signature
	{
		IDxcOperationResult* compiled_rootsig_res = compile_dxc_file(file_dxc_buffer, {
			"-T", "rootsig_1_1",
			"-E", "ROOT_SIGNATURE",
			}).claim();

		// claim root signature data
		HRESULT status;
		compiled_rootsig_res->GetStatus(&status);
		if (SUCCEEDED(status))
		{
			compiled_rootsig_res->GetResult((IDxcBlob**)&result_entry.m_blob_rootsignature);
		}
		else
		{
			IDxcBlobEncoding* errors = nullptr;
			if (SUCCEEDED(compiled_rootsig_res->GetErrorBuffer(&errors)) && errors)
			{
				const char* text = static_cast<const char*>(errors->GetBufferPointer());
				size_t size = errors->GetBufferSize();
				std::string errorLog(text, size);
				OutputDebugStringA(errorLog.c_str());
				errors->Release();
				return restype::make_fail("rootsignature failed parsing!");
			}
		}
	}
	// compile shader bytecode
	{
		IDxcOperationResult* compiled_shader_res = compile_dxc_file(file_dxc_buffer, {
			"-T", string(target),
			"-E", string(entrypoint),
			}).claim();

		compiled_shader_res->GetStatus(&hres);
		if (SUCCEEDED(hres))
		{
			compiled_shader_res->GetResult((IDxcBlob**)&result_entry.m_shaderlib);
		}
		else
		{
			IDxcBlobEncoding* errors = nullptr;
			if (SUCCEEDED(compiled_shader_res->GetErrorBuffer(&errors)) && errors)
			{
				const char* text = static_cast<const char*>(errors->GetBufferPointer());
				size_t size = errors->GetBufferSize();
				std::string errorLog(text, size);
				OutputDebugStringA(errorLog.c_str());
				errors->Release();
			}
		}
	}
	// build state object (and other resources based on compile info)
	{
		release_if_valid(result_entry.m_signature);
		auto rootsigblob = result_entry.m_blob_rootsignature;
		HRESULT hres = device.CreateRootSignature(0, rootsigblob->GetBufferPointer(), rootsigblob->GetBufferSize(), IID_PPV_ARGS(&result_entry.m_signature));
		if (!SUCCEEDED(hres))
		{
			return restype::make_fail("failed creating root signature");
		}

		auto shadercodeblob = result_entry.m_shaderlib;
		result_entry.m_shader_bytecode.pShaderBytecode = shadercodeblob->GetBufferPointer();
		result_entry.m_shader_bytecode.BytecodeLength = shadercodeblob->GetBufferSize();
	}

	return restype::make_success();
}

static const char* k_vs_target = "vs_6_5";
static const char* k_ps_target = "ps_6_5";
void renderman::compile_shaders()
{
	static bool once = true;
	if (!once) return;
	once = false;

	using restype = result<>;
	m_shaderman.compile_shader(
		"D:/Git/strikers/hlsl/shading.hlsl",
		"main_vs",
		k_vs_target,
		*m_device).claim();
	  
	m_shaderman.compile_shader(
		"D:/Git/strikers/hlsl/shading.hlsl",
		"main_ps",
		k_ps_target,
		*m_device).claim();
	
	compile_pipelines();
}

void renderman::compile_pipelines()
{
	using restype = result<>;

	m_pipelines.push_back({});

	// configure the pipeline here
	{
		m_pipelines[0].m_shaders_filepath = "D:/Git/strikers/hlsl/shading.hlsl";
		m_pipelines[0].m_ps_entrypoint = "main_ps";
		m_pipelines[0].m_vs_entrypoint = "main_vs";
		m_pipelines[0].m_ps_target = k_ps_target;
		m_pipelines[0].m_vs_target = k_vs_target;
		
		m_pipelines[0].m_desc = {};
		m_pipelines[0].m_desc.Flags;

		m_pipelines[0].m_input_elements.push_back({});
		m_pipelines[0].m_input_elements.back().AlignedByteOffset = 0;
		m_pipelines[0].m_input_elements.back().Format = DXGI_FORMAT_R32G32B32_FLOAT;
		m_pipelines[0].m_input_elements.back().InputSlot = 0;
		m_pipelines[0].m_input_elements.back().InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		m_pipelines[0].m_input_elements.back().InstanceDataStepRate = 0;
		m_pipelines[0].m_input_elements.back().SemanticIndex = 0;
		m_pipelines[0].m_input_elements.back().SemanticName = "SV_POSITION";

		m_pipelines[0].m_input_elements.push_back({});
		m_pipelines[0].m_input_elements.back().AlignedByteOffset = sizeof(float3);
		m_pipelines[0].m_input_elements.back().Format = DXGI_FORMAT_R32G32B32_FLOAT;
		m_pipelines[0].m_input_elements.back().InputSlot = 0;
		m_pipelines[0].m_input_elements.back().InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		m_pipelines[0].m_input_elements.back().InstanceDataStepRate = 0;
		m_pipelines[0].m_input_elements.back().SemanticIndex = 0;
		m_pipelines[0].m_input_elements.back().SemanticName = "NORMAL";

		m_pipelines[0].m_desc.InputLayout.NumElements = (uint32)m_pipelines[0].m_input_elements.size();
		m_pipelines[0].m_desc.InputLayout.pInputElementDescs = m_pipelines[0].m_input_elements.data();
		m_pipelines[0].m_desc.BlendState.AlphaToCoverageEnable = false;
		m_pipelines[0].m_desc.BlendState.IndependentBlendEnable = false;
		m_pipelines[0].m_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		m_pipelines[0].m_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		m_pipelines[0].m_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		m_pipelines[0].m_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		m_pipelines[0].m_desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		m_pipelines[0].m_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		m_pipelines[0].m_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		m_pipelines[0].m_desc.DepthStencilState.DepthEnable = false;
		m_pipelines[0].m_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		m_pipelines[0].m_desc.DepthStencilState.DepthWriteMask;
		m_pipelines[0].m_desc.DepthStencilState.StencilEnable = false;
		m_pipelines[0].m_desc.DepthStencilState.StencilReadMask = 0;
		m_pipelines[0].m_desc.DepthStencilState.StencilWriteMask = 0;
		m_pipelines[0].m_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		m_pipelines[0].m_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		m_pipelines[0].m_desc.RasterizerState.AntialiasedLineEnable = false;
		m_pipelines[0].m_desc.RasterizerState.ConservativeRaster;
		m_pipelines[0].m_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		m_pipelines[0].m_desc.RasterizerState.DepthBias = 0;
		m_pipelines[0].m_desc.RasterizerState.DepthBiasClamp = 0;
		m_pipelines[0].m_desc.RasterizerState.DepthClipEnable = false;
		m_pipelines[0].m_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		m_pipelines[0].m_desc.RasterizerState.ForcedSampleCount;
		m_pipelines[0].m_desc.RasterizerState.FrontCounterClockwise = true;
		m_pipelines[0].m_desc.RasterizerState.MultisampleEnable;
		m_pipelines[0].m_desc.RasterizerState.SlopeScaledDepthBias;
		m_pipelines[0].m_desc.SampleDesc.Count = 1;
		m_pipelines[0].m_desc.SampleDesc.Quality = 0;
		m_pipelines[0].m_desc.SampleMask = 0xFFFFFFFF;
		m_pipelines[0].m_desc.NumRenderTargets = 1;
		for (uint32 i = 0u; i < m_pipelines[0].m_desc.NumRenderTargets; ++i)
		{
			m_pipelines[0].m_desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].BlendEnable = TRUE;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
			m_pipelines[0].m_desc.BlendState.RenderTarget[i].RenderTargetWriteMask = 0xf;
		}
	}

	// here we create the actual dx12 stuff
	for (graphics_pipeline& pipeline : m_pipelines)
	{
		const auto& vs_shader = m_shaderman.get_shader(
			pipeline.m_shaders_filepath, pipeline.m_vs_entrypoint, pipeline.m_vs_target).claim();
		const auto& ps_shader = m_shaderman.get_shader(
			pipeline.m_shaders_filepath, pipeline.m_ps_entrypoint, pipeline.m_ps_target).claim();

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = pipeline.m_desc;
		pipeline_desc.PS = ps_shader->m_shader_bytecode;
		pipeline_desc.VS = vs_shader->m_shader_bytecode;
		pipeline_desc.pRootSignature = vs_shader->m_signature;
		pipeline.m_dxsignature = vs_shader->m_signature;

		HRESULT hres = m_device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline.m_dxpipeline));
		if (!SUCCEEDED(hres))
		{
			int a = 0; // todo: do something...
		}
	}
}

template <typename _t, typename _fn>
result<> map_resource(dxresource& resource, _fn&& func)
{
	using restype = result<>;
	void* out_address;
	HRESULT hres = resource.Map(0, nullptr, &out_address);
	if (!SUCCEEDED(hres))
	{
		return restype::make_fail("");
	}

	func((_t*)out_address);
	resource.Unmap(0, nullptr);
	return result<>::make_success();
}

result<> renderman::initialize()
{
	using restype = result<>;
	restype result{};

	UINT factory_flags = 0;

	// enable debug layer
#if defined(DF_DEBUG)
	ID3D12Debug* debug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
	{
		debug->EnableDebugLayer();
		factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
		debug->Release();
	}
#endif
	result = make_result<restype>(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&m_factory)));
	if (result.is_fail()) return result;

	// enumerate adapters (GPUs) - the first to create the device succesfully is the chosen one
	IDXGIAdapter1* chosen_adapter = nullptr;
	D3D12_WORK_GRAPHS_TIER workgraphs_tier = D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;
	for (UINT i = 0; m_factory->EnumAdapters1(i, &chosen_adapter) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_ADAPTER_DESC1 desc;
		chosen_adapter->GetDesc1(&desc);

		// skip software gpus
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		ID3D12Device1* device = nullptr;
		D3D12CreateDevice(chosen_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
		if (device)
		{
			m_adapter = chosen_adapter;
			m_device = device;
			break;

			D3D12_FEATURE_DATA_D3D12_OPTIONS21 options = {};
			device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &options, sizeof(options));
			workgraphs_tier = options.WorkGraphsTier;
			if (workgraphs_tier != D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED)
			{
				m_adapter = chosen_adapter;
				m_device = device;
				break;
			}
			else
			{
				device->Release();
			}
		}
	}

	if (m_adapter == nullptr || m_device == nullptr)
	{
		return restype::make_fail("renderman::initialize() failed > querying all adapters, none could create the graphics device!");
	}

	// create command queue
	{
		D3D12_COMMAND_QUEUE_DESC desc{};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		result = make_result<restype>(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue)));
		if (result.is_fail()) return result;
	}
	// create main commandlist & allocator
	{
		result = make_result<restype>(m_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&m_cmd_allocator)));
		if (result.is_fail()) return result;

		result = make_result<restype>(m_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_cmd_allocator,
			nullptr,
			IID_PPV_ARGS(&m_cmdlist)));
		if (result.is_fail()) return result;

		result = make_result<restype>(m_cmdlist->Close());
		if (result.is_fail()) return result;
	}
	// create all descriptor heaps
	{
		for (uint32 i = 0; i < k_num_descriptor_heaps; ++i)
		{
			const descriptor_heap heap = (descriptor_heap)i;

			const D3D12_DESCRIPTOR_HEAP_TYPE heap_type = get_descheap_type(heap);
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
			heap_desc.Type = heap_type;
			heap_desc.NumDescriptors = get_descheap_size(heap);
			heap_desc.Flags = (is_descheap_gpu_readable(heap) ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
			result = make_result<restype>(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_descheaps[i].m_dxheap)));

			get_descheap(heap).m_handle_size = m_device->GetDescriptorHandleIncrementSize(heap_type);
		}
	}
	// create fence
	{
		result = make_result<restype>(m_device->CreateFence(
			m_frame,
			D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(&m_frame_fence)));
	}
	// constantbuffer
	{
		const uint64 constbuffer_bytesize = max(256, sizeof(gpu_cbuffer));
		m_constantbuffer = allocate_gpu_buffer(*m_device, constbuffer_bytesize, sizeof(constbuffer_bytesize), false, D3D12_HEAP_TYPE_UPLOAD).claim();
		m_constantbuffer_cbv = create_resource_descriptor(*m_constantbuffer, descriptor_type::cbv, false).claim();
	}
	return result;
}

void renderman::render(renderscene& scene, const contentman& cman)
{
	D3D12_VIEWPORT viewport{};
	viewport.Height = 480;
	viewport.Width = 640;
	viewport.MinDepth = 0;
	viewport.MaxDepth = 1;
	D3D12_RECT scissor{};
	scissor.left = 0;
	scissor.right = 640;
	scissor.bottom = 480;
	scissor.top = 0;

	// process the scene
	if (!scene.m_instances.empty())
	{
		// register instance mesh buffers
		for (uint32 i = 0u; i < scene.m_instances.size(); ++i)
		{
			auto& instance = scene.m_instances[i];
			const auto mesh_asset_res = cman.find_mesh_asset(instance.m_mesh);
			if (mesh_asset_res.is_fail())
			{
				instance.m_ignored = true;
				continue;
			}

			const auto& mesh_asset = mesh_asset_res.claim();

			// allocate the mesh buffers once
			if (!m_mesh_buffers.contains(instance.m_mesh))
			{
				const uint64 vertbuffer_size = mesh_asset->calculate_vertexbuffer_bytesize() + mesh_asset->calculate_normalbuffer_bytesize();
				const uint64 indbuffer_size = mesh_asset->calculate_indexbuffer_bytesize();
				meshbuffers& meshbuffs = m_mesh_buffers[instance.m_mesh];
				meshbuffs.m_vertbuffer = allocate_gpu_buffer(*m_device, vertbuffer_size, sizeof(gpu_vertex), false, D3D12_HEAP_TYPE_UPLOAD).claim();
				meshbuffs.m_indbuffer = allocate_gpu_buffer(*m_device, indbuffer_size, sizeof(uint32), false, D3D12_HEAP_TYPE_UPLOAD).claim();
				meshbuffs.m_vtb_view.BufferLocation = meshbuffs.m_vertbuffer->GetGPUVirtualAddress();
				meshbuffs.m_vtb_view.SizeInBytes = (uint32)vertbuffer_size;
				meshbuffs.m_vtb_view.StrideInBytes = sizeof(gpu_vertex);
				meshbuffs.m_idx_view.BufferLocation = meshbuffs.m_indbuffer->GetGPUVirtualAddress();
				meshbuffs.m_idx_view.Format = DXGI_FORMAT_R32_UINT;
				meshbuffs.m_idx_view.SizeInBytes = (uint32)indbuffer_size;
				
				map_resource<uint32>(*meshbuffs.m_indbuffer, [&mesh_asset, indbuffer_size](uint32* index) {
					memcpy(index, mesh_asset->m_indices.data(), indbuffer_size);
				}).claim();
				
				map_resource<gpu_vertex>(*meshbuffs.m_vertbuffer, [&mesh_asset](gpu_vertex* vertex) {
					for (uint32 i = 0u; i < mesh_asset->m_vertices.size(); ++i)
					{
						(*vertex).m_position = mesh_asset->m_vertices[i];
						(*vertex).m_normal = mesh_asset->m_normals[i];
						++vertex;
					}
				}).claim();
			}
		}

		// (re) allocate the instance buffer
		const uint32 num_instances = scene.m_instances.size();;
		const uint64 instancebuffer_bytesize = sizeof(gpu_instance) * num_instances;
		if (m_instancebuffer == nullptr || m_instancebuffer->GetDesc().Width < instancebuffer_bytesize)
		{
			release_if_valid(m_instancebuffer);
			m_instancebuffer = allocate_gpu_buffer(*m_device, instancebuffer_bytesize, sizeof(gpu_instance), false, D3D12_HEAP_TYPE_UPLOAD).claim();
			
			descriptor_args srv_builder{};
			srv_builder.m_buffer_srv.NumElements = num_instances;
			srv_builder.m_buffer_srv.StructureByteStride = sizeof(gpu_instance);
			m_instancebuffer_srv = create_resource_descriptor(*m_instancebuffer, descriptor_type::srv, false, srv_builder).claim();
		}

		// update instance buffer
		map_resource<gpu_instance>(*m_instancebuffer, [&scene](gpu_instance* instance)
		{
			for (const auto& pair : scene.m_batch_instance_lookup)
			{
				const auto& batch_key = pair.first;
				const vector<uint32>& batch_indices = pair.second;
				for (const uint32& index : batch_indices)
				{
					(*instance).m_transform = scene.m_instances[index].m_transform.m_matrix;
					(*instance).m_color = scene.m_instances[index].m_color;
					instance++;
				}
			}
		});
	
		// update constant buffer
		map_resource<gpu_cbuffer>(*m_constantbuffer, [&scene, &viewport](gpu_cbuffer* cbuffer)
		{
			const auto mat_view = calculate_view_mat(scene.m_camera.m_transform.m_matrix);
			const auto mat_proj = calculate_proj_mat(scene.m_camera.m_fov, (float)viewport.Width / (float)viewport.Height, scene.m_camera.m_near, scene.m_camera.m_far);
			cbuffer->m_viewprojection = mat_proj * mat_view;
			cbuffer->m_light_color = scene.m_light.m_color;
			cbuffer->m_light_direction = scene.m_light.m_direction;
		});
	}

	// render
	uint64 fence_value = m_frame_fence->GetCompletedValue();
	while (fence_value < m_frame)
	{
		fence_value = m_frame_fence->GetCompletedValue();
	}

	const uint32 current_swapchain_idx = 0;
	swapchain& swapchain = m_swapchains[current_swapchain_idx];
	dxresource& current_backbuffer = swapchain.get_current_backbuffer_resource();
	dxresource& uav_proxy = *swapchain.m_uav_proxy_resource;
	const descriptor& current_backbuffer_rtv = swapchain.get_current_backbuffer_rtv();
	const descheap& rtv_heap = get_descheap(descriptor_heap::rtv);
	D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv_handle;
	rtv_heap.get_handles(current_backbuffer_rtv.m_heap_idx, &backbuffer_rtv_handle).claim();

	clear_gpu_heaps();

	m_cmd_allocator->Reset();
	m_cmdlist->Reset(m_cmd_allocator, nullptr);

	// bind bindless resource heaps
	dxdescheap* global_descheaps[]{
		get_descheap(descriptor_heap::gpu_resource).m_dxheap,
		get_descheap(descriptor_heap::gpu_sampler).m_dxheap
	};
	m_cmdlist->SetDescriptorHeaps(_countof(global_descheaps), global_descheaps);
	
	cmd_transition_barrier(current_backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	
	static float k_clear_color[4]{ 0.1,0.1,0.1,1 };
	m_cmdlist->ClearRenderTargetView(backbuffer_rtv_handle, k_clear_color, 0, nullptr);
	m_cmdlist->OMSetRenderTargets(1, &backbuffer_rtv_handle, true, nullptr);
	m_cmdlist->SetPipelineState(m_pipelines[0].m_dxpipeline);
	m_cmdlist->SetGraphicsRootSignature(m_pipelines[0].m_dxsignature);
	m_cmdlist->RSSetViewports(1, &viewport);
	m_cmdlist->RSSetScissorRects(1, &scissor);

	if (!scene.m_instances.empty() && !scene.m_batch_instance_lookup.empty())
	{
		m_cmdlist->SetGraphicsRootConstantBufferView(0, m_constantbuffer->GetGPUVirtualAddress());
		m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer->GetGPUVirtualAddress());
		for (const auto& pair : scene.m_batch_instance_lookup)
		{
			const auto& mesh_id = pair.first;
			const meshbuffers& mesh_buffers = m_mesh_buffers.at(mesh_id);
			const auto& instances = pair.second;
			const uint32 num_instances = instances.size();
			if (num_instances == 0)
			{
				continue;
			}

			dxresource const* indexbuffer = mesh_buffers.m_indbuffer;
			dxresource const* vertbuffer = mesh_buffers.m_vertbuffer;
			const uint32 batch_first_instance = scene.get_batch_first_instance(mesh_id);
			const uint32 num_indices = mesh_buffers.get_num_indices();
			m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_cmdlist->IASetVertexBuffers(0u, 1, &mesh_buffers.m_vtb_view);
			m_cmdlist->IASetIndexBuffer(&mesh_buffers.m_idx_view);
			m_cmdlist->DrawIndexedInstanced(
				num_indices,
				num_instances,
				0,
				0,
				batch_first_instance);
		}
	}

	cmd_transition_barrier(current_backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_cmdlist->Close();

	// submit to GPU
	{
		vector<ID3D12CommandList*> cmdlists;
		cmdlists.push_back(m_cmdlist);
		m_queue->ExecuteCommandLists((uint32)cmdlists.size(), cmdlists.data());
		m_queue->Signal(m_frame_fence, ++m_frame);
	}

	// present
	swapchain.m_swapchain->Present(0, 0);
}
}