#include "renderman.h"
#include "contentman.h"
#include "logman.h"

static const char* k_vs_target = "vs_6_8";
static const char* k_vs_entry = "main_vs";
static const char* k_ps_target = "ps_6_8";
static const char* k_ps_entry = "main_ps";
static const char* k_cs_target = "cs_6_8";
static const char* k_cs_entry = "main_cs";

static const char* k_ui_shader_filepath			= DF_FOLDER_SHADERS "ui.hlsl";
static const char* k_shadows_shader_filepath	= DF_FOLDER_SHADERS "shadows.hlsl";
static const char* k_shading_shader_filepath	= DF_FOLDER_SHADERS "shading.hlsl";
static const char* k_skinning_shader_filepath	= DF_FOLDER_SHADERS "skinning.hlsl";
static const char* k_outline_shader_filepath	= DF_FOLDER_SHADERS "outline.hlsl";
static const char* k_lines_shader_filepath		= DF_FOLDER_SHADERS "lines.hlsl";
static const char* k_quads_shader_filepath		= DF_FOLDER_SHADERS "quads.hlsl";
static const char* k_shader_include_folder		= "D:/Git/strikers/hlsl";

static const char* k_vsps_entrypoints[]{ k_vs_entry, k_ps_entry };
static const char* k_vsps_targets[] { k_vs_target, k_ps_target };
static const char* k_vsps_shaders[] {
	k_ui_shader_filepath,
	k_shading_shader_filepath,
	k_lines_shader_filepath,
	k_quads_shader_filepath
};
static const char* k_vs_shaders[]{
	k_shadows_shader_filepath
};

namespace strikers {
static string hr_to_string(HRESULT hr)
{
	char* msg = nullptr;
	DWORD flags =
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS;

	DWORD size = FormatMessageA(
		flags,
		nullptr,
		hr,
		0,
		(LPSTR)&msg,
		0,
		nullptr
	);

	string result;
	if (size && msg) result = msg;
	else result = "Unknown HRESULT";

	if (msg) LocalFree(msg);
	return result;
}
static DxcBuffer blob_encoding_to_dxc(IDxcBlobEncoding* encoding)
{
	DxcBuffer result;
	int encoding_known = false; uint32 code_page;
	if (SUCCEEDED(encoding->GetEncoding(&encoding_known, &code_page)))
	{
		result.Encoding = code_page;
	}

	result.Ptr = encoding->GetBufferPointer();
	result.Size = encoding->GetBufferSize();
	return result;
}
static void release_if_valid(IUnknown* anything)
{
	if (anything != nullptr)
	{
		anything->Release();
	}
}
static void release_if_valid(gpu_resource& resource)
{
	release_if_valid(resource.m_resource);
}

result<> shaderman::compile_shader(
	const stringview& filepath, 
	const stringview& entrypoint,
	const stringview& target,
	dxdevice& device)
{
	using restype = result<>;

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
	// compile shader bytecode
	{
		IDxcOperationResult* compiled_shader_res = compile_dxc_file(file_dxc_buffer, {
			"-T", string(target),
			"-E", string(entrypoint),
			"-Zi",
			"-I", k_shader_include_folder,
			"-Qembed_debug" // for debugging in profilers
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
	// compile root signature
	{
		IDxcOperationResult* compiled_rootsig_res = compile_dxc_file(file_dxc_buffer, {
			"-T", "rootsig_1_1",
			"-E", "ROOT_SIGNATURE",
			"-I", k_shader_include_folder
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

result<> renderman::initialize_pipelines()
{
	using restype = result<>;
	static bool once = true;
	if (!once) return {};
	once = false;

	m_pipelines.resize(pipeline::num_total);

	// configure all pipelines
	{
		m_pipelines[pipeline::skinning] = pipeline(pipeline::builder()
			.filepath(k_skinning_shader_filepath)
			.entrypoint(shader::cs, k_cs_entry)
			.target(shader::cs, k_cs_target)
		);

		m_pipelines[pipeline::outline] = pipeline(pipeline::builder()
			.filepath(k_outline_shader_filepath)
			.entrypoint(shader::cs, k_cs_entry)
			.target(shader::cs, k_cs_target)
		);

		// mesh pipelines (using mesh vertex buffers)
		{
			pipeline::builder common_builder{};
			common_builder
				// shaders
				.filepath(k_shading_shader_filepath)
				.entrypoint(shader::vs, k_vs_entry)
				.entrypoint(shader::ps, k_ps_entry)
				.target(shader::vs, k_vs_target)
				.target(shader::ps, k_ps_target)
				// vs_inputs
				.push_mesh_vertex_inputs()
				// depth & stencil
				.enable_depth(true)
				.depth_function(D3D12_COMPARISON_FUNC_LESS)
				.depth_write_mask(D3D12_DEPTH_WRITE_MASK_ALL)
				.dsv_format(DXGI_FORMAT_D32_FLOAT)
				// rasterizer
				.primitive_topology_type(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
				.cullmode(D3D12_CULL_MODE_NONE)
				.fillmode(D3D12_FILL_MODE_SOLID)
				// sampling
				.sample_count(1)
				.sample_mask(0xFFFFFFFF)
				// render targets
				.rt_format(0, DXGI_FORMAT_R8G8B8A8_UNORM)
				.rt_write_mask(0, 0xf)
				.rt_blend_source_to_dest(0, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_BLEND_INV_SRC_ALPHA)
				.rt_blend_alpha_source_to_dest(0, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD, D3D12_BLEND_INV_SRC_ALPHA);
			m_pipelines[pipeline::shading] = pipeline(common_builder);

			// shaded - wireframe
			common_builder
				.fillmode(D3D12_FILL_MODE_WIREFRAME);
			m_pipelines[pipeline::wireframe] = pipeline(common_builder);

			// shadows
			common_builder
				.fillmode(D3D12_FILL_MODE_SOLID)
				.filepath(k_shadows_shader_filepath)
				.entrypoint(shader::ps, "")
				.target(shader::ps, "");
			m_pipelines[pipeline::shadows] = pipeline(common_builder);
		}
		
		// special geometry pipelines
		{
			pipeline::builder common_builder{}; common_builder
				.filepath(k_shading_shader_filepath)
				.entrypoint(shader::vs, k_vs_entry)
				.entrypoint(shader::ps, k_ps_entry)
				.target(shader::vs, k_vs_target)
				.target(shader::ps, k_ps_target)
				// depth & stencil
				.enable_depth(true)
				.depth_function(D3D12_COMPARISON_FUNC_LESS)
				.depth_write_mask(D3D12_DEPTH_WRITE_MASK_ALL)
				.dsv_format(DXGI_FORMAT_D32_FLOAT)
				// rasterizer
				.cullmode(D3D12_CULL_MODE_NONE)
				.fillmode(D3D12_FILL_MODE_SOLID)
				// sampling
				.sample_count(1)
				.sample_mask(0xFFFFFFFF)
				// render targets
				.rt_format(0, DXGI_FORMAT_R8G8B8A8_UNORM)
				.rt_write_mask(0, 0xf)
				.rt_blend_source_to_dest(0, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_BLEND_INV_SRC_ALPHA)
				.rt_blend_alpha_source_to_dest(0, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD, D3D12_BLEND_INV_SRC_ALPHA);
		
			common_builder
				.filepath(k_lines_shader_filepath)
				.primitive_topology_type(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
			m_pipelines[pipeline::lines] = pipeline(common_builder);

			common_builder
				.filepath(k_quads_shader_filepath)
				.primitive_topology_type(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
			m_pipelines[pipeline::quads] = pipeline(common_builder);

			common_builder
				.filepath(k_ui_shader_filepath)
				.primitive_topology_type(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
			m_pipelines[pipeline::ui] = pipeline(common_builder);
		}
	}

	// compile all pipelines' shaders
	for (uint32 p = 0u; p < m_pipelines.size(); ++p)
	{
		const pipeline& pip = m_pipelines[p];
		for (uint32 s = 0; s < shader::num; ++s)
		{
			const string& shader_entrypoint = pip.m_builder.m_entrypoints[s];
			const string& shader_target = pip.m_builder.m_targets[s];
			if (!shader_entrypoint.empty() && !shader_target.empty())
				m_shaderman.compile_shader(pip.m_builder.m_filepath, shader_entrypoint, shader_target, *m_device).claim();
		}
	}

	// compile all pipelines
	{
		for (uint32 p = 0u; p < m_pipelines.size(); ++p)
		{
			pipeline& pip = m_pipelines[p];
			if (pipeline::is_compute(p))
			{
				const auto& cs_shader = m_shaderman.get_shader(
					pip.m_builder.m_filepath, pip.m_builder.m_entrypoints[shader::cs], pip.m_builder.m_targets[shader::cs]).claim();

				D3D12_COMPUTE_PIPELINE_STATE_DESC& pipeline_desc = pip.m_builder.m_compute_desc;
				pipeline_desc.CS = cs_shader->m_shader_bytecode;
				pipeline_desc.pRootSignature = cs_shader->m_signature;
				pip.m_dxsignature = cs_shader->m_signature;
				HRESULT hres = m_device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pip.m_dxpipeline));
				if (!SUCCEEDED(hres))
				{
					int a = 0; // todo: do something...
				}
			}
			else if (pipeline::is_graphics(p))
			{
				pip.m_builder.m_graphics_desc.InputLayout.NumElements = (uint32)pip.m_builder.m_input_elements.size();
				pip.m_builder.m_graphics_desc.InputLayout.pInputElementDescs = pip.m_builder.m_input_elements.data();

				const string& filepath = pip.m_builder.m_filepath;
				const string& vs_entrypoint = pip.m_builder.m_entrypoints[shader::vs];
				const string& ps_entrypoint = pip.m_builder.m_entrypoints[shader::ps];
				const string& vs_target = pip.m_builder.m_targets[shader::vs];
				const string& ps_target = pip.m_builder.m_targets[shader::ps];
				const auto& vs_shader = m_shaderman.get_shader(filepath, vs_entrypoint, vs_target).claim();

				D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipeline_desc = pip.m_builder.m_graphics_desc;
				pipeline_desc.VS = vs_shader->m_shader_bytecode;
				pipeline_desc.pRootSignature = vs_shader->m_signature;
				pip.m_dxsignature = vs_shader->m_signature;

				if (!ps_entrypoint.empty() && !ps_target.empty())
				{
					auto& ps_shader = m_shaderman.get_shader(filepath, ps_entrypoint, ps_target).claim();
					if (ps_shader) pipeline_desc.PS = ps_shader->m_shader_bytecode;
				}

				HRESULT hres = m_device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pip.m_dxpipeline));
				if (!SUCCEEDED(hres))
				{
					int a = 0; // todo: do something...
				}
			}
		}
	}

	return {};
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
	if (!SUCCEEDED(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&m_factory))))
	{
		return restype::make_fail("CreateDXGIFactory2 failed!");
	}

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

#if defined(DF_DEBUG)
	ID3D12InfoQueue* info_queue;
	auto rest = m_device->QueryInterface(IID_PPV_ARGS(&info_queue));
	if (SUCCEEDED(rest))
	{
		static D3D12_MESSAGE_CATEGORY deny_categories[]
		{
			D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED
		};
		static D3D12_MESSAGE_ID deny_ids[]
		{
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
			D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
			D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED
		};
		static D3D12_MESSAGE_SEVERITY deny_sevs[]
		{
			D3D12_MESSAGE_SEVERITY_MESSAGE
		};

		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumCategories = _countof(deny_categories);
		filter.DenyList.pCategoryList = deny_categories;
		filter.DenyList.NumIDs = _countof(deny_ids);
		filter.DenyList.pIDList = deny_ids;
		filter.DenyList.NumSeverities = _countof(deny_sevs);
		filter.DenyList.pSeverityList = deny_sevs;
		info_queue->AddStorageFilterEntries(&filter);
	}
#endif

	// create command queue
	{
		D3D12_COMMAND_QUEUE_DESC desc{};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (!SUCCEEDED(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue))))
		{
			return restype::make_fail("Device::CreateCommandQueue() failed!");
		}
	}
	// create main commandlist & allocator
	{
		if (!SUCCEEDED(m_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&m_cmd_allocator))))
		{
			return restype::make_fail("Device::CreateCommandAllocator() failed!");
		}			

		if (!SUCCEEDED(m_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_cmd_allocator,
			nullptr,
			IID_PPV_ARGS(&m_cmdlist))))
		{
			return restype::make_fail("Device::CreateCommandList() failed!");
		}

		if (!SUCCEEDED(m_cmdlist->Close()))
		{
			return restype::make_fail("Commandlist::Close() failed!");
		}
	}
	// create all descriptor heaps
	{
		for (uint32 i = 0; i < descriptor_heap::num; ++i)
		{
			const descriptor_heap::slot heap = (descriptor_heap::slot)i;

			const D3D12_DESCRIPTOR_HEAP_TYPE heap_type = descriptor_heap::dxtype(heap);
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
			heap_desc.Type = heap_type;
			heap_desc.NumDescriptors = descriptor_heap::capacity(heap);
			heap_desc.Flags = (descriptor_heap::is_gpu_heap(heap) ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
			if (!SUCCEEDED(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_descheaps[i].m_dxheap))))
			{
				return restype::make_fail("Device::CreateDescriptorHeap() failed!");
			}

			get_descheap(heap).m_handle_size = m_device->GetDescriptorHandleIncrementSize(heap_type);
		}
	}
	// create fence
	{
		if (!SUCCEEDED(m_device->CreateFence(m_frame, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frame_fence))))
		{
			return restype::make_fail("Device::CreateFence() failed!");
		}
	}
	// allocate system constantbuffers
	{
		m_cbuffers.ensure_allocated(*this, cbuffer::global, 0);
		m_cbuffers.ensure_allocated(*this, cbuffer::slot::view, view::main); // main view
		m_cbuffers.ensure_allocated(*this, cbuffer::slot::view, view::light); // light view
	}
	// allocate shadow map
	{
		m_shadows.m_resource = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(4096, 4096)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
			.format(DXGI_FORMAT_R32_TYPELESS)
			.init_state(D3D12_RESOURCE_STATE_DEPTH_READ)
		).claim();
		m_shadows.m_dsv = create_resource_descriptor(m_shadows.m_resource, descriptor::builder()
			.type(descriptor::dsv)
			.format(DXGI_FORMAT_D32_FLOAT)
		).claim();
		m_shadows.m_srv = create_resource_descriptor(m_shadows.m_resource, descriptor::builder()
			.type(descriptor::srv)
			.format(DXGI_FORMAT_R32_FLOAT)
		).claim();
	}
	// allocate dummy buffers
	{
		m_dummies.m_buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.buffer_single(sizeof(float) * 16)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
		).claim();
	}

	const uint2 scenecolor_resolution = { 1280, 720 };

	// allocate bitmap
	{
		m_bitmap.m_resource = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(scenecolor_resolution)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			.format(DXGI_FORMAT_R8_TYPELESS)
		).claim();
		m_bitmap.m_uav = create_resource_descriptor(m_bitmap.m_resource, descriptor::builder()
			.type(descriptor::uav)
			.format(DXGI_FORMAT_R8_UINT)
		).claim();
		m_bitmap.m_srv = create_resource_descriptor(m_bitmap.m_resource, descriptor::builder()
			.type(descriptor::srv)
			.format(DXGI_FORMAT_R8_UINT)
		).claim();
	}
	// allocate scenecolor
	{
		
		m_scenecolor.m_resource_color = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(scenecolor_resolution)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
			.format(DXGI_FORMAT_R8G8B8A8_UNORM)
		).claim();
		m_scenecolor.m_resource_depth = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(scenecolor_resolution)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
			.format(DXGI_FORMAT_D32_FLOAT)
		).claim();

		m_scenecolor.m_uav = create_resource_descriptor(m_scenecolor.m_resource_color, descriptor::builder()
			.type(descriptor::uav)
			.format(DXGI_FORMAT_R8G8B8A8_UNORM)
		).claim();
		m_scenecolor.m_srv = create_resource_descriptor(m_scenecolor.m_resource_color, descriptor::builder()
			.type(descriptor::srv)
			.format(DXGI_FORMAT_R8G8B8A8_UNORM)
		).claim();
		m_scenecolor.m_rtv = create_resource_descriptor(m_scenecolor.m_resource_color, descriptor::builder()
			.type(descriptor::rtv)
			.format(DXGI_FORMAT_R8G8B8A8_UNORM)
		).claim();
		m_scenecolor.m_dsv = create_resource_descriptor(m_scenecolor.m_resource_depth, descriptor::builder()
			.type(descriptor::dsv)
			.format(DXGI_FORMAT_D32_FLOAT)
		).claim();
	}

	initialize_pipelines().claim();

	return result;
}

void renderman::render_meshbatches(renderscene& scene, const contentman& cman, const meshbatch_filter& filter)
{
	for (const auto& pair : scene.m_batch_instance_lookup)
	{
		const renderscene::batch_key& batch_key = pair.first;
		const mesh_id mesh = batch_key.m_mesh;
		if (!m_mesh_buffers.contains(mesh))
		{
			continue;
		}

		// apply batch filters
		if ((filter.m_flags & meshbatch_filter::fl_shader_equal) != 0 && batch_key.m_shader != filter.m_shader) {
			continue;
		}
		if ((filter.m_flags & meshbatch_filter::fl_shader_nequal) != 0 && batch_key.m_shader == filter.m_shader) {
			continue;
		}
		if ((filter.m_flags & meshbatch_filter::fl_mesh_equal) != 0 && batch_key.m_mesh != filter.m_shader) {
			continue;
		}
		if ((filter.m_flags & meshbatch_filter::fl_mesh_nequal) != 0 && batch_key.m_mesh == filter.m_shader) {
			continue;
		}

		meshbuffers& mesh_buffers = m_mesh_buffers.at(mesh);
		const auto& instances = pair.second;
		const uint32 num_mesh_instances = (uint32)instances.size();
		if (num_mesh_instances == 0)
		{
			continue;
		}

		string draw_marker = "mesh_batch";
		auto found_mesh_in_content = cman.find_mesh(mesh);
		if (found_mesh_in_content)
		{
			draw_marker = std::format("mesh: {}", found_mesh_in_content->m_name.c_str());
		}

		PIXScopedEvent(m_cmdlist, 0u, draw_marker.c_str());

		mesh_buffers.m_vtb_view.BufferLocation = mesh_buffers.m_vertbuffer.m_resource->GetGPUVirtualAddress();
		mesh_buffers.m_vtb_view.SizeInBytes = (uint32)mesh_buffers.m_vertbuffer.m_resource->GetDesc().Width;
		mesh_buffers.m_vtb_view.StrideInBytes = sizeof(gpu_vertex);
		mesh_buffers.m_idx_view.BufferLocation = mesh_buffers.m_indbuffer.m_resource->GetGPUVirtualAddress();
		mesh_buffers.m_idx_view.Format = DXGI_FORMAT_R32_UINT;
		mesh_buffers.m_idx_view.SizeInBytes = (uint32)mesh_buffers.m_indbuffer.m_resource->GetDesc().Width;

		dxresource const* indexbuffer = mesh_buffers.m_indbuffer.m_resource;
		dxresource const* vertbuffer = mesh_buffers.m_vertbuffer.m_resource;
		const uint32 batch_first_instance = scene.get_meshbatch_first_instance(batch_key);
		const uint32 num_indices = mesh_buffers.get_num_indices();
		m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmdlist->IASetVertexBuffers(0u, 1, &mesh_buffers.m_vtb_view);
		m_cmdlist->IASetIndexBuffer(&mesh_buffers.m_idx_view);
		m_cmdlist->DrawIndexedInstanced(
			num_indices,
			num_mesh_instances,
			0,
			0,
			batch_first_instance);
	}
}

void renderman::render(renderscene& scene, const contentman& cman)
{
	process_scene_instances(scene, cman);

	// wait for previous frame
	uint64 fence_value = m_frame_fence->GetCompletedValue();
	while (fence_value < m_frame)
	{
		fence_value = m_frame_fence->GetCompletedValue();
	}

	const uint32 current_swapchain_idx = 0;
	swapchain& swapchain = m_swapchains[current_swapchain_idx];
	const uint2 backbuffer_size = swapchain.m_current_size;

	m_cmd_allocator->Reset();
	m_cmdlist->Reset(m_cmd_allocator, nullptr);

	// clear & re-bind global GPU descriptor heaps
	clear_gpu_heaps();
	
	dxdescheap* global_descheaps[]{
		get_descheap(descriptor_heap::gpu_resource).m_dxheap,
		get_descheap(descriptor_heap::gpu_sampler).m_dxheap
	};
	m_cmdlist->SetDescriptorHeaps(_countof(global_descheaps), global_descheaps);
	m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// upload all pending textures & meshbuffer copies (stored in staging)
	upload_cpu_to_gpu();
	
	// [skinning]
	gpu_resource& bone_buffer = *gpu_resource_with_fallback(m_bone_buffers.m_bone_buffer).claim();
	gpu_resource& bone_instance_buffer = *gpu_resource_with_fallback(m_bone_buffers.m_bone_instance_buffer).claim();
	gpu_resource& anim_channels_buffer = *gpu_resource_with_fallback(m_anim_buffers.m_buffers[animation_buffers::channels]).claim();
	gpu_resource& anim_keyframes_buffer = *gpu_resource_with_fallback(m_bone_buffers.m_bone_instance_buffer).claim();
	state_barrier(bone_instance_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	state_barrier(anim_channels_buffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	state_barrier(anim_keyframes_buffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	const uint32 num_bone_instances = m_bone_buffers.total_num_bone_instances();
	if (num_bone_instances > 0)
	{
		PIXScopedEvent(m_cmdlist, 0u, "[cpip_skinning]");

		m_cmdlist->SetPipelineState(m_pipelines[pipeline::skinning].m_dxpipeline);
		m_cmdlist->SetComputeRootSignature(m_pipelines[pipeline::skinning].m_dxsignature);
		m_cmdlist->SetComputeRootConstantBufferView(0u, m_cbuffers.resource(cbuffer::global).gpu_address());
		m_cmdlist->SetComputeRootShaderResourceView(1u, bone_buffer.gpu_address());
		m_cmdlist->SetComputeRootShaderResourceView(2u, anim_channels_buffer.gpu_address());
		m_cmdlist->SetComputeRootShaderResourceView(3u, anim_keyframes_buffer.gpu_address());
		m_cmdlist->SetComputeRootUnorderedAccessView(4u, bone_instance_buffer.gpu_address());

		static const uint32 group_size = 64; // todo: make this non-hardcoded? honestly not a big deal...
		const uint32 num_groups = (num_bone_instances + group_size - 1) / group_size;
		m_cmdlist->Dispatch(num_groups, 1, 1);
	}
	state_barrier(bone_instance_buffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	
	const descheap& rtv_heap = get_descheap(descriptor_heap::rtv);
	const descheap& dsv_heap = get_descheap(descriptor_heap::dsv);

	// [shadow rendering]
	if (scene.any_meshes())
	{
		D3D12_RESOURCE_DESC shadowmap_dxdesc{};
		if (!m_shadows.m_resource.get_dxdesc(shadowmap_dxdesc))
		{
			return;
		}

		state_barrier(m_shadows.m_resource, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		D3D12_CPU_DESCRIPTOR_HANDLE shadowmap_dsv_handle;
		dsv_heap.get_handles(m_shadows.m_dsv.m_heap_idx, &shadowmap_dsv_handle);
		m_cmdlist->OMSetRenderTargets(0u, nullptr, false, &shadowmap_dsv_handle);

		float clear_depth_value = 1.0f;
		if (m_shadows.m_resource.m_builder.m_has_clear_value)
		{
			clear_depth_value = m_shadows.m_resource.m_builder.m_clear_value_depth;
		}
		m_cmdlist->ClearDepthStencilView(shadowmap_dsv_handle, D3D12_CLEAR_FLAG_DEPTH, clear_depth_value, 0u, 0u, nullptr);
		const uint2 shadowmap_size = { shadowmap_dxdesc.Width, shadowmap_dxdesc.Height };
		D3D12_VIEWPORT viewport{};
		viewport.Height = (float)shadowmap_size.x;
		viewport.Width = (float)shadowmap_size.y;
		viewport.MinDepth = 0;
		viewport.MaxDepth = 1;
		D3D12_RECT scissor{};
		scissor.left = 0;
		scissor.right = shadowmap_size.x;
		scissor.bottom = shadowmap_size.y;
		scissor.top = 0;
		m_cmdlist->RSSetViewports(1, &viewport);
		m_cmdlist->RSSetScissorRects(1, &scissor);

		PIXScopedEvent(m_cmdlist, 0u, "[pip_shadows]");
		m_cmdlist->SetPipelineState(m_pipelines[pipeline::shadows].m_dxpipeline);
		m_cmdlist->SetGraphicsRootSignature(m_pipelines[pipeline::shadows].m_dxsignature);
		m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::view, view::light).gpu_address());
		m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer.gpu_address());
		m_cmdlist->SetGraphicsRootShaderResourceView(2, bone_instance_buffer.gpu_address());

		render_meshbatches(scene, cman);

		state_barrier(m_shadows.m_resource, D3D12_RESOURCE_STATE_DEPTH_READ);
	}

	// [scenecolor rendering]
	state_barrier(m_scenecolor.m_resource_color, D3D12_RESOURCE_STATE_RENDER_TARGET);
	state_barrier(m_scenecolor.m_resource_depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	{
		D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv_handle, backbuffer_dsv_handle;
		rtv_heap.get_handles(m_scenecolor.m_rtv.m_heap_idx, &backbuffer_rtv_handle).claim();
		dsv_heap.get_handles(m_scenecolor.m_dsv.m_heap_idx, &backbuffer_dsv_handle).claim();

		D3D12_VIEWPORT viewport{};
		viewport.Height = (float)backbuffer_size.y;
		viewport.Width = (float)backbuffer_size.x;
		viewport.MinDepth = 0;
		viewport.MaxDepth = 1;
		D3D12_RECT scissor{};
		scissor.left = 0;
		scissor.right = backbuffer_size.x;
		scissor.bottom = backbuffer_size.y;
		scissor.top = 0;

		static float k_clear_color[4]{ 0.1f,0.1f,0.1f,1.0f };
		m_cmdlist->ClearRenderTargetView(backbuffer_rtv_handle, k_clear_color, 0, nullptr);
		m_cmdlist->ClearDepthStencilView(backbuffer_dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0u, nullptr);
		m_cmdlist->OMSetRenderTargets(1, &backbuffer_rtv_handle, true, &backbuffer_dsv_handle);
		m_cmdlist->RSSetViewports(1, &viewport);
		m_cmdlist->RSSetScissorRects(1, &scissor);

		state_barrier(m_bitmap.m_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		// mesh passes
		// > pip_shading
		// > pip_wireframe
		if (scene.any_meshes())
		{
			static const uint32 k_num_shaders = modelshader::num;
			static const pipeline::slot k_pipelines[k_num_shaders]
			{
				pipeline::shading,
				pipeline::wireframe
			};
			static const char* k_shader_names[]
			{
				"pip_shading",
				"pip_wireframe"
			};

			// for each shader, draw a bunch of shaded instances
			for (uint32 i = 0u; i < k_num_shaders; ++i)
			{
				const string scope_name = std::format("[{}]", k_shader_names[i]);
				PIXScopedEvent(m_cmdlist, 0u, scope_name.c_str());

				m_cmdlist->SetPipelineState(m_pipelines[k_pipelines[i]].m_dxpipeline);
				m_cmdlist->SetGraphicsRootSignature(m_pipelines[k_pipelines[i]].m_dxsignature);
				m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::global).gpu_address());
				m_cmdlist->SetGraphicsRootConstantBufferView(1, m_cbuffers.resource(cbuffer::view, view::main).gpu_address());
				m_cmdlist->SetGraphicsRootShaderResourceView(2, m_instancebuffer.gpu_address());
				m_cmdlist->SetGraphicsRootShaderResourceView(3, bone_instance_buffer.gpu_address());

				// render the mesh batches only that match the current shader
				render_meshbatches(scene, cman, meshbatch_filter()
					.shader_equal((shader::slot)i));
			}
		}

		// line passes: pip_lines
		if (!scene.m_line_instances.empty())
		{
			PIXScopedEvent(m_cmdlist, 0u, "lines");
			m_cmdlist->SetPipelineState(m_pipelines[pipeline::lines].m_dxpipeline);
			m_cmdlist->SetGraphicsRootSignature(m_pipelines[pipeline::lines].m_dxsignature);
		
			// update lines instance buffer
			const uint32 num_instances = (uint32)scene.m_line_instances.size();
			m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
			m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::global).m_resource->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootConstantBufferView(1, m_cbuffers.resource(cbuffer::view, view::main).m_resource->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootShaderResourceView(2, m_instancebuffer_lines.m_resource->GetGPUVirtualAddress());
			m_cmdlist->DrawInstanced(2, num_instances, 0, 0);
		}

		// quad passes: pip_quads
		if (!scene.m_quad_instances.empty())
		{
			PIXScopedEvent(m_cmdlist, 0u, "quads");
			m_cmdlist->SetPipelineState(m_pipelines[pipeline::quads].m_dxpipeline);
			m_cmdlist->SetGraphicsRootSignature(m_pipelines[pipeline::quads].m_dxsignature);
		
			const uint32 num_instances = (uint32)scene.m_quad_instances.size();
			m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::global).m_resource->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootConstantBufferView(1, m_cbuffers.resource(cbuffer::view, view::main).m_resource->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootShaderResourceView(2, m_instancebuffer_quads.m_resource->GetGPUVirtualAddress());
			m_cmdlist->DrawInstanced(6, num_instances, 0, 0);
		}

		// ui passes: pip_ui
		if (!scene.m_ui_instances.empty())
		{
			PIXScopedEvent(m_cmdlist, 0u, "UI");
		
			m_cmdlist->SetPipelineState(m_pipelines[pipeline::ui].m_dxpipeline);
			m_cmdlist->SetGraphicsRootSignature(m_pipelines[pipeline::ui].m_dxsignature);
			const uint32 num_instances = (uint32)scene.m_ui_instances.size();
			m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer_ui.m_resource->GetGPUVirtualAddress());
			m_cmdlist->DrawInstanced(6, num_instances, 0, 0);
		}
	}

	// [scenecolor post-processing]
	// > outline
	state_barrier(m_scenecolor.m_resource_color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	state_barrier(m_bitmap.m_resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	{
		PIXScopedEvent(m_cmdlist, 0u, "[outline]");

		m_cmdlist->SetPipelineState(m_pipelines[pipeline::outline].m_dxpipeline);
		m_cmdlist->SetComputeRootSignature(m_pipelines[pipeline::outline].m_dxsignature);
		m_cmdlist->SetComputeRootConstantBufferView(0u, m_cbuffers.resource(cbuffer::global).gpu_address());

		static const uint32 group_size = 8; // todo: make this non-hardcoded? honestly not a big deal...
		m_cmdlist->Dispatch(backbuffer_size.x / group_size, backbuffer_size.y / group_size, 1);
	}

	// [scenecolor > copy > backbuffer]
	gpu_resource& current_backbuffer = swapchain.get_current_backbuffer_resource();
	state_barrier(m_scenecolor.m_resource_color, D3D12_RESOURCE_STATE_COPY_SOURCE);
	state_barrier(current_backbuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	{
		const descriptor& current_backbuffer_rtv = swapchain.get_current_backbuffer_rtv();

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = m_scenecolor.m_resource_color.m_resource;
		src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		src.SubresourceIndex = 0;
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = current_backbuffer.m_resource;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		m_cmdlist->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}
	state_barrier(current_backbuffer, D3D12_RESOURCE_STATE_PRESENT);

	// submit our frame commands to the GPU
	m_cmdlist->Close();
	vector<ID3D12CommandList*> cmdlists;
	cmdlists.push_back(m_cmdlist);
	m_queue->ExecuteCommandLists((uint32)cmdlists.size(), cmdlists.data());
	m_queue->Signal(m_frame_fence, ++m_frame);
	swapchain.m_swapchain->Present(0, 0);
}

void renderman::process_scene_instances(renderscene& scene, const contentman& cman)
{
	// reset all skeleton instances
	for (auto& pair : m_bone_buffers.m_skeleton_infos)
	{
		pair.second.m_instances.clear();
	}

	// here we reallocate our GPU buffers if crossing the current capacity
	if (!scene.m_ui_instances.empty())
	{
		// register ui_instance textures
		for (uint32 i = 0u; i < scene.m_ui_instances.size(); ++i)
		{
			const renderscene::ui_instance& instance = scene.m_ui_instances[i];
			reallocate_image_texture(cman, instance.m_image);
		}

		// (re) allocate the ui instance buffer
		const uint32 num_ui_instances = (uint32)scene.m_ui_instances.size();;
		const uint64 ui_instancebuffer_bytesize = sizeof(gpu_ui_instance) * num_ui_instances;
		if (!m_instancebuffer_ui.is_valid() || m_instancebuffer_ui.m_resource->GetDesc().Width < ui_instancebuffer_bytesize)
		{
			release_if_valid(m_instancebuffer_ui);
			m_instancebuffer_ui = gpu_resource::allocate(*m_device, gpu_resource::builder()
				.buffer_single(ui_instancebuffer_bytesize)
				.init_state(D3D12_RESOURCE_STATE_GENERIC_READ)
				.heap_type(D3D12_HEAP_TYPE_UPLOAD)
			).claim();

			m_instancebuffer_ui_srv = create_resource_descriptor(m_instancebuffer_ui, descriptor::builder()
				.type(descriptor::srv)
				.bff_first_element(0u)
				.bff_num_elements(num_ui_instances)
				.bff_bytestride(sizeof(gpu_ui_instance))
			).claim();
		}
	}
	if (!scene.m_mesh_instances.empty())
	{
		// register instance mesh buffers
		for (uint32 i = 0u; i < scene.m_mesh_instances.size(); ++i)
		{
			auto& instance = scene.m_mesh_instances[i];
			const auto mesh_asset_res = cman.find_typed_asset<asset_type::mesh>(instance.m_mesh);
			if (mesh_asset_res.is_fail())
			{
				continue;
			}

			// reallocate gpu buffers (only if new data enters)
			reallocate_image_texture(cman, instance.m_img_basecolor);
			const bool skeleton_loaded = reallocate_skeleton_buffers(cman, instance.m_skeleton);
			const bool animation_loaded = reallocate_animation_buffers(cman, instance.m_animation);

			// insert new skeleton instance
			if (skeleton_loaded)
			{
				auto& skeleton = m_bone_buffers.m_skeleton_infos[instance.m_skeleton];
				bone_buffers::skeleton_instance_info skeleton_instance{};
				skeleton_instance.m_time = instance.m_time;
				if (animation_loaded)
				{
					// for each channel in our animation, link the bone instance to the channel idx
					const auto& animation = m_anim_buffers.m_animations[m_anim_buffers.m_animation_to_idx[instance.m_animation]];
					for (const auto& pair : animation.m_name_to_channel_idx)
					{
						const auto& name = pair.first;
						const auto& channel_idx = pair.second;
						if (skeleton.m_bone_name_to_idx.contains(name))
						{
							const auto& bone_index = skeleton.m_bone_name_to_idx[name];
							skeleton_instance.m_bone_index_to_channel_idx[bone_index] = channel_idx;
						}
					}
				}
				skeleton.m_instances.push_back(skeleton_instance);
				instance.m_skeleton_instance_idx = (uint32)skeleton.m_instances.size() - 1u;
			}
			
			// (re)allocate the mesh buffers
			if (!m_mesh_buffers.contains(instance.m_mesh))
			{
				const auto& mesh_asset = mesh_asset_res.claim();
				const uint64 vertbuffer_size = mesh_asset->get_num_vertices() * sizeof(gpu_vertex);
				const uint64 indbuffer_size = mesh_asset->m_indices.size() * sizeof(uint32);
				meshbuffers& meshbuffs = m_mesh_buffers[instance.m_mesh];

				meshbuffs.m_vertbuffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
					.buffer_single(vertbuffer_size)
					.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
				).claim();

				meshbuffs.m_vertex_stagingbuffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
					.buffer_single(vertbuffer_size)
					.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
					.heap_type(D3D12_HEAP_TYPE_UPLOAD)
				).claim();

				meshbuffs.m_indbuffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
					.buffer_single(indbuffer_size)
					.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
				).claim();

				meshbuffs.m_index_stagingbuffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
					.buffer_single(indbuffer_size)
					.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
					.heap_type(D3D12_HEAP_TYPE_UPLOAD)
				).claim();
				
				map_resource<uint32>(meshbuffs.m_index_stagingbuffer, [&mesh_asset, indbuffer_size](uint32* index) {
					memcpy(index, mesh_asset->m_indices.data(), indbuffer_size);
				}).claim();

				map_resource<gpu_vertex>(meshbuffs.m_vertex_stagingbuffer, [&mesh_asset](gpu_vertex* vertex) {
					for (uint32 i = 0u; i < mesh_asset->m_vertices.size(); ++i)
					{
						const mesh_asset::vertex& in_vertex = mesh_asset->m_vertices[i];
						(*vertex).position = in_vertex.m_position;
						(*vertex).normal = in_vertex.m_normal;
						(*vertex).uv = { in_vertex.m_uv.x, in_vertex.m_uv.y };
						(*vertex).bone_weights = in_vertex.m_bone_weights;
						(*vertex).bone_ids = in_vertex.m_bone_indices;
						++vertex;
					}
				}).claim();
			}
		}

		// (re) allocate the instance buffer
		const uint32 num_mesh_instances = (uint32)scene.m_mesh_instances.size();
		const uint64 instancebuffer_bytesize = sizeof(gpu_instance) * num_mesh_instances;
		if (!m_instancebuffer.is_valid() || m_instancebuffer.m_resource->GetDesc().Width < instancebuffer_bytesize)
		{
			release_if_valid(m_instancebuffer);
			m_instancebuffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
				.buffer_single(instancebuffer_bytesize)
				.init_state(D3D12_RESOURCE_STATE_GENERIC_READ)
				.heap_type(D3D12_HEAP_TYPE_UPLOAD)
			).claim();

			m_instancebuffer_srv = create_resource_descriptor(m_instancebuffer, descriptor::builder()
				.type(descriptor::srv)
				.bff_bytestride(sizeof(gpu_instance))
				.bff_num_elements(num_mesh_instances)
			).claim();
		}
	}
	if (!scene.m_line_instances.empty())
	{
		const uint32 num_lines = (uint32)scene.m_line_instances.size();
		const uint64 bytesize = sizeof(gpu_line_instance) * num_lines;
		if (m_instancebuffer_lines.buffer_needs_realloc(bytesize))
		{
			release_if_valid(m_instancebuffer_lines);
			m_instancebuffer_lines = gpu_resource::allocate(*m_device, gpu_resource::builder()
				.buffer_single(bytesize)
				.init_state(D3D12_RESOURCE_STATE_COMMON)
				.heap_type(D3D12_HEAP_TYPE_UPLOAD)).claim();

			m_instancebuffer_lines_srv = create_resource_descriptor(m_instancebuffer_lines, descriptor::builder()
				.type(descriptor::srv)
				.bff_num_elements(num_lines)
				.bff_bytestride(sizeof(gpu_line_instance))
			).claim();
		}
	}
	if (!scene.m_quad_instances.empty())
	{
		for (uint32 i = 0u; i < scene.m_quad_instances.size(); ++i)
		{
			reallocate_image_texture(cman, scene.m_quad_instances[i].m_image);
		}

		const uint32 num_quads = (uint32)scene.m_quad_instances.size();
		const uint64 bytesize = sizeof(gpu_quad_instance) * num_quads;
		if (m_instancebuffer_quads.buffer_needs_realloc(bytesize))
		{
			release_if_valid(m_instancebuffer_quads);
			m_instancebuffer_quads = gpu_resource::allocate(*m_device, gpu_resource::builder()
				.buffer_single(bytesize)
				.init_state(D3D12_RESOURCE_STATE_COMMON)
				.heap_type(D3D12_HEAP_TYPE_UPLOAD)).claim();

			m_instancebuffer_quads_srv = create_resource_descriptor(m_instancebuffer_quads, descriptor::builder()
				.type(descriptor::srv)
				.bff_num_elements(num_quads)
				.bff_bytestride(sizeof(gpu_quad_instance))
			).claim();
		}
	}

	// now that we have counted each instance of each skeleton, 
	// count up the total instanced bones & assign each skeleton a range
	uint32 total_sum_bone_instances = 0u;
	for (auto& pair : m_bone_buffers.m_skeleton_infos)
	{
		const uint32 num_bones = (uint32)pair.second.m_gpu_bones.size();
		const uint32 num_instances = (uint32)pair.second.m_instances.size();
		pair.second.m_bone_instance_offset = total_sum_bone_instances;
		total_sum_bone_instances += (num_bones * num_instances);
	}

	// reallocate the bone instance buffer if needed (based on how many bone instances we have)
	gpu_resource& bone_instance_buffer = m_bone_buffers.m_bone_instance_buffer;
	gpu_resource& bone_instance_upload = m_bone_buffers.m_bone_instance_upload;
	const uint32 total_num_bone_instances = m_bone_buffers.total_num_bone_instances();
	const uint32 bone_instances_bytesize = (uint32)sizeof(gpu_bone_instance) * total_num_bone_instances;
	if (bone_instance_buffer.buffer_needs_realloc(bone_instances_bytesize))
	{
		release_if_valid(bone_instance_buffer);
		release_if_valid(bone_instance_upload);

		bone_instance_buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.type(gpu_resource::buffer)
			.bytestride(sizeof(gpu_bone_instance))
			.bytesize(bone_instances_bytesize)
			.init_state(D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
		).claim();

		bone_instance_upload = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.type(gpu_resource::buffer)
			.bytestride(sizeof(gpu_bone_instance))
			.bytesize(bone_instances_bytesize)
			.init_state(D3D12_RESOURCE_STATE_GENERIC_READ)
			.heap_type(D3D12_HEAP_TYPE_UPLOAD)
		).claim();
	}

	// write instance buffers contents
	if (scene.any_meshes())
	{
		map_resource<gpu_instance>(m_instancebuffer, [this, &scene](gpu_instance* instance) {
			for (const auto& pair : scene.m_batch_instance_lookup)
			{
				const auto& batch_key = pair.first;
				const vector<uint32>& batch_indices = pair.second;
				for (const uint32& instance_index : batch_indices)
				{
					const auto& instance_data = scene.m_mesh_instances[instance_index];
					(*instance).transform = instance_data.m_transform.m_matrix;
					(*instance).color = instance_data.m_color;
					(*instance).bone_instance_offset.set_null();
					(*instance).texid_basecolor.set_null();
					(*instance).bitflags = (uint8)instance_data.m_bitflags;

					const auto skel_instance = instance_data.m_skeleton_instance_idx;
					if (instance_data.m_skeleton != k_id_invalid && skel_instance != (uint32)-1)
					{
						const auto& skeleton = m_bone_buffers.m_skeleton_infos[instance_data.m_skeleton];
						const uint32 skeleton_instance_offset = (uint32)skeleton.m_gpu_bones.size() * instance_data.m_skeleton_instance_idx;
						const uint32 skeleton_bone_offset = skeleton.m_bone_instance_offset;
						(*instance).bone_instance_offset = skeleton_bone_offset + skeleton_instance_offset;
					}

					const image_id tex_basecolor = instance_data.m_img_basecolor;
					if (m_image_textures.contains(tex_basecolor))
					{
						(*instance).texid_basecolor = m_image_textures.at(tex_basecolor).m_gpu_heap_slot;
					}
					instance++;
				}
			}
		});
	}
	if (m_bone_buffers.total_num_bone_instances() > 0)
	{
		map_resource<gpu_bone_instance>(m_bone_buffers.m_bone_instance_upload, [this](gpu_bone_instance* gpu_instances) 
		{
			for (auto& pair : m_bone_buffers.m_skeleton_infos)
			{
				const auto& skeleton = pair.second;
				for (uint32 i = 0u; i < skeleton.m_instances.size(); ++i)
				{
					const bone_buffers::skeleton_instance_info& skeleton_instance = skeleton.m_instances[i];
					const uint32 num_bones = (uint32)skeleton.m_gpu_bones.size();
					for (uint32 b = 0u; b < num_bones; ++b)
					{
						const uint32 bone_instance_idx = skeleton.m_bone_instance_offset + ((i * num_bones) + b);
						gpu_instances[bone_instance_idx].bone_index = skeleton.m_bone_offset + b;
						gpu_instances[bone_instance_idx].anim_channel_index.set_null();
						gpu_instances[bone_instance_idx].anim_time = skeleton_instance.m_time;

						if (skeleton_instance.m_bone_index_to_channel_idx.contains(b))
						{
							gpu_instances[bone_instance_idx].anim_channel_index = (int)skeleton_instance.m_bone_index_to_channel_idx.at(b);
						}
					}
				}
			}
		});
	}
	if (!scene.m_quad_instances.empty())
	{
		const uint32 num_instances = (uint32)scene.m_quad_instances.size();
		map_resource<gpu_quad_instance>(m_instancebuffer_quads, [this, &scene, num_instances](gpu_quad_instance* instance) {
			for (uint32 q = 0u; q < scene.m_quad_instances.size(); ++q)
			{
				const renderscene::quad_instance& quad = scene.m_quad_instances[q];
				instance[q].color = quad.m_color;
				instance[q].transform = quad.m_transform.m_matrix;
				instance[q].rect_uv = quad.m_rect_uv.m_min_max;

				instance[q].texid_color.set_null();
				if (m_image_textures.contains(quad.m_image))
				{
					instance[q].texid_color = m_image_textures.at(quad.m_image).m_gpu_heap_slot;
				}
			}
		});
	}
	if (!scene.m_line_instances.empty())
	{
		const uint32 num_instances = (uint32)scene.m_line_instances.size();
		map_resource<gpu_line_instance>(m_instancebuffer_lines, [this, &scene, num_instances](gpu_line_instance* instance) {
			memcpy(instance, scene.m_line_instances.data(), sizeof(gpu_line_instance) * num_instances);
		});
	}
	if (!scene.m_ui_instances.empty())
	{
		const uint32 num_instances = (uint32)scene.m_ui_instances.size();
		map_resource<gpu_ui_instance>(m_instancebuffer_ui, [this, &scene, num_instances](gpu_ui_instance* instance) {
			memcpy(instance, scene.m_ui_instances.data(), sizeof(gpu_ui_instance) * num_instances);
			for (const auto& cpu_instance : scene.m_ui_instances)
			{
				instance->texture_heap_id.set_null();
				if (m_image_textures.contains(cpu_instance.m_image))
				{
					instance->texture_heap_id = m_image_textures.at(cpu_instance.m_image).m_gpu_heap_slot;
				}
				instance++;
			}
		});
	}
	
	// write all constant buffers
	{
		const uint32 current_swapchain_idx = 0;
		swapchain& swapchain = m_swapchains[current_swapchain_idx];
		const uint2 backbuffer_size = swapchain.m_current_size;

		// cbuffer: light view (shadows)
		m_cbuffers.write_data(cbuffer::view, view::light, [&scene](void* dest) {
			gpu_cbuffer_view* dest_view = reinterpret_cast<gpu_cbuffer_view*>(dest);
			const auto mat_view = calculate_view_mat(scene.m_light.m_transform.m_matrix);
			const float orthographic_size = 10.0f;
			const auto& frustrum = scene.m_light.m_frustrum;
			const auto& frustrum_min = frustrum.abs_min();
			const auto& frustrum_max = frustrum.abs_max();
			const auto mat_proj = calculate_orthographic_proj_mat(
				{ frustrum_min.x, frustrum_max.x },
				{ frustrum_min.y, frustrum_max.y },
				{ frustrum_min.z, frustrum_max.z });
			dest_view->viewprojection = mat_proj * mat_view;
			scene.m_light.m_mat_to_lightspace = dest_view->viewprojection;
		});

		// cbuffer: global
		m_cbuffers.write_data(cbuffer::global, 0u, [this, backbuffer_size, &scene, total_num_bone_instances](void* dest) {
			gpu_cbuffer_global* global = reinterpret_cast<gpu_cbuffer_global*>(dest);
			global->light_color = scene.m_light.m_color;
			global->light_direction = float4(-scene.m_light.m_transform.get_forward(), 1);
			global->mat_to_lightspace = scene.m_light.m_mat_to_lightspace;
			global->num_bone_instances = total_num_bone_instances;
			global->outline_params = float4(1,1,1,1);
			push_gpu_resource_descriptor(*m_device, m_shadows.m_srv, global->texid_shadows);
			push_gpu_resource_descriptor(*m_device, m_bitmap.m_uav, global->texid_bitmap_uav);
			push_gpu_resource_descriptor(*m_device, m_scenecolor.m_uav, global->texid_scenecolor_uav);
		});

		// cbuffer: main view
		m_cbuffers.write_data(cbuffer::view, view::main, [&scene, &backbuffer_size](void* dest) {
			gpu_cbuffer_view* dest_view = reinterpret_cast<gpu_cbuffer_view*>(dest);
			const auto mat_view = calculate_view_mat(scene.m_camera_transform.m_matrix);
			const auto mat_proj = calculate_perspective_proj_mat(scene.m_camera.m_fov_vertical,
				(float)backbuffer_size.x / (float)backbuffer_size.y,
				scene.m_camera.m_clip_near,
				scene.m_camera.m_clip_far);
			dest_view->viewprojection = mat_proj * mat_view;
			dest_view->screen_size = backbuffer_size;
		});
	}
}

void renderman::upload_cpu_to_gpu()
{
	for (auto& pair : m_mesh_buffers)
	{
		meshbuffers& buffers = pair.second;
		if (!buffers.m_uploaded)
		{
			m_cmdlist->CopyResource(buffers.m_vertbuffer.m_resource, buffers.m_vertex_stagingbuffer.m_resource);
			m_cmdlist->CopyResource(buffers.m_indbuffer.m_resource, buffers.m_index_stagingbuffer.m_resource);
			state_barrier(buffers.m_vertbuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			state_barrier(buffers.m_indbuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			buffers.m_uploaded = true;
		}
	}

	if (m_bone_buffers.m_needs_upload)
	{
		m_cmdlist->CopyResource(m_bone_buffers.m_bone_buffer.m_resource, m_bone_buffers.m_bone_upload.m_resource);
		state_barrier(m_bone_buffers.m_bone_buffer, D3D12_RESOURCE_STATE_GENERIC_READ);
		m_bone_buffers.m_needs_upload = false;
	}

	if (m_anim_buffers.m_needs_upload)
	{
		for (uint32 b = 0u; b < animation_buffers::num; ++b)
		{
			m_cmdlist->CopyResource(m_anim_buffers.m_buffers[b].m_resource, m_anim_buffers.m_staging[b].m_resource);
			state_barrier(m_anim_buffers.m_buffers[b], D3D12_RESOURCE_STATE_GENERIC_READ);
		}
		m_anim_buffers.m_needs_upload = false;
	}

	// always upload the bone instance data
	// map bone instances -> bone data
	if (m_bone_buffers.m_bone_instance_buffer.m_resource && m_bone_buffers.m_bone_instance_upload.m_resource)
	{
		state_barrier(m_bone_buffers.m_bone_instance_buffer, D3D12_RESOURCE_STATE_COPY_DEST);
		state_barrier(m_bone_buffers.m_bone_instance_upload, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_cmdlist->CopyResource(m_bone_buffers.m_bone_instance_buffer.m_resource, m_bone_buffers.m_bone_instance_upload.m_resource);
	}
	
	for (auto& pair : m_image_textures)
	{
		texture& tex = pair.second;
		if (!tex.m_uploaded)
		{
			UINT DstX{};
			UINT DstY{};
			UINT DstZ{};

			const uint32 pixel_bytesize = 4u;
			const uint32 texture_width = (uint32)tex.m_gpu_resource.m_resource->GetDesc().Width;
			const uint32 texture_height = (uint32)tex.m_gpu_resource.m_resource->GetDesc().Height;

			D3D12_TEXTURE_COPY_LOCATION source{};
			source.PlacedFootprint.Offset = 0;
			source.PlacedFootprint.Footprint.Width = texture_width;
			source.PlacedFootprint.Footprint.Height = texture_height;
			source.PlacedFootprint.Footprint.Depth = 1;
			source.PlacedFootprint.Footprint.Format = tex.m_gpu_resource.m_resource->GetDesc().Format;
			source.PlacedFootprint.Footprint.RowPitch = (uint32)(texture_width * pixel_bytesize);
			source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			source.pResource = tex.m_staging_resource.m_resource;
			D3D12_TEXTURE_COPY_LOCATION dest{};
			dest.pResource = tex.m_gpu_resource.m_resource;
			dest.SubresourceIndex = 0;
			dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			m_cmdlist->CopyTextureRegion(&dest, DstX, DstY, DstZ, &source, nullptr);
			state_barrier(tex.m_gpu_resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			tex.m_uploaded = true;
		}

		// push each texture onto the gpu heap
		push_gpu_resource_descriptor(*m_device, tex.m_srv, tex.m_gpu_heap_slot);
	}
}

bool renderman::reallocate_skeleton_buffers(const contentman& cman, skel_id id)
{
	if (id == k_id_invalid)
	{
		return false;
	}

	const auto found_asset_res = cman.find_typed_asset<asset_type::skeleton>(id);
	if (found_asset_res.is_fail())
	{
		return false;
	}

	// can't re-allocate bone buffers am afraid...
	const bool already_allocated = m_bone_buffers.m_skeleton_infos.contains(id);
	if (already_allocated)
	{
		return true;
	}

	const auto& skeleton_asset = found_asset_res.claim();
	const auto& bones = skeleton_asset->m_bones;
	const uint32 num_bones = (uint32)bones.size();

	// cache the gpu-bones of this skeleton on the CPU
	auto& skeleton_infos = m_bone_buffers.m_skeleton_infos[id];
	skeleton_infos.m_bone_offset = m_bone_buffers.m_total_num_bones;
	m_bone_buffers.m_total_num_bones += num_bones;
	skeleton_infos.m_gpu_bones.resize(num_bones);
	for (uint32 i = 0u; i < bones.size(); ++i)
	{
		skeleton_infos.m_gpu_bones[i].transform = bones[i].m_mat_transform;
		skeleton_infos.m_gpu_bones[i].inverse_bind = bones[i].m_mat_inverse_bind;
		skeleton_infos.m_gpu_bones[i].parent = (bones[i].m_valid_parent ? 
			(skeleton_infos.m_bone_offset + bones[i].m_parent_idx) : -1);
		skeleton_infos.m_bone_name_to_idx[bones[i].m_name] = i;
	}

	gpu_resource& bone_buffer = m_bone_buffers.m_bone_buffer;
	gpu_resource& upload_buffer = m_bone_buffers.m_bone_upload;
	const uint32 bones_bytesize_needed = (uint32)sizeof(gpu_bone) * m_bone_buffers.m_total_num_bones;
	if (bone_buffer.buffer_needs_realloc(bones_bytesize_needed))
	{
		release_if_valid(bone_buffer);
		release_if_valid(upload_buffer);

		upload_buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.type(gpu_resource::buffer)
			.bytestride(sizeof(gpu_bone))
			.bytesize(bones_bytesize_needed)
			.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
			.heap_type(D3D12_HEAP_TYPE_UPLOAD)).claim();

		// map everything to the upload resource
		map_resource<gpu_bone>(upload_buffer, [this](gpu_bone* dest) 
		{
			for (const auto& pair : m_bone_buffers.m_skeleton_infos)
			{
				const auto& info = pair.second;
				for (uint32 i = 0u; i < info.m_gpu_bones.size(); ++i)
				{
					const auto& bone = info.m_gpu_bones[i];
					dest[info.m_bone_offset + i] = bone;
				}
			}
		}).claim();

		bone_buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.type(gpu_resource::buffer)
			.bytestride(sizeof(gpu_bone))
			.bytesize(bones_bytesize_needed)
			.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
		).claim();

		m_bone_buffers.m_needs_upload = true;

#if 0
		m_bone_buffers.m_bone_buffer_srv = create_resource_descriptor(bone_buffer, descriptor::builder()
			.type(descriptor::srv)
			.bff_bytestride(sizeof(gpu_bone))
			.bff_first_element(0u)
			.bff_num_elements(new_total_num_bones)
		).claim();
#endif
	}

	return true;
}

bool renderman::reallocate_animation_buffers(const contentman& cman, anim_id id)
{
	if (id == k_id_invalid)
	{
		return false;
	}

	const auto found_asset_res = cman.find_typed_asset<asset_type::animation>(id);
	if (found_asset_res.is_fail())
	{
		return false;
	}

	// skip reallocate
	if (m_anim_buffers.m_animation_to_idx.contains(id))
	{
		return true;
	}

	// allocate a new animation
	const auto& anim_asset = *found_asset_res.claim();
	auto& channels = m_anim_buffers.m_channels;
	auto& keyframes = m_anim_buffers.m_keyframes;
	auto& animations = m_anim_buffers.m_animations;

	const uint32 new_animation_index = (uint32)animations.size();
	m_anim_buffers.m_animation_to_idx[id] = new_animation_index;
	
	// assemble all offsets & counts
	animation_buffers::animation_info new_animation{};
	new_animation.m_first_channel = channels.size();
	for (uint32 c = 0u; c < anim_asset.m_channels.size(); ++c)
	{
		hlsl::animation_channel new_channel{};
		new_channel.first_keyframe = keyframes.size();
		for (uint32 k = 0u; k < anim_asset.m_channels[c].m_position_keys.size(); ++k)
		{
			const auto& position_key = anim_asset.m_channels[c].m_position_keys[k];
			const auto& rotation_key = anim_asset.m_channels[c].m_rotation_keys[k];
			const auto& scale_key = anim_asset.m_channels[c].m_scale_keys[k];

			hlsl::keyframe new_keyframe{};
			new_keyframe.position = position_key.m_value;
			new_keyframe.rotation = rotation_key.m_value;
			new_keyframe.scale = scale_key.m_value;
			keyframes.push_back(new_keyframe);
		}
		new_channel.num_keyframes = keyframes.size() - new_channel.first_keyframe;
		
		new_animation.m_name_to_channel_idx[anim_asset.m_channels[c].m_name] = c;
		channels.push_back(new_channel);
	}
	new_animation.m_num_channels = channels.size() - new_animation.m_first_channel;
	animations.push_back(new_animation);

	// reallocate buffer resources
	const uint64 bytesizes[animation_buffers::num]
	{
		animation_buffers::bytestride(animation_buffers::channels) * channels.size(),
		animation_buffers::bytestride(animation_buffers::keyframes) * keyframes.size()
	};
	for (uint32 b = 0u; b < animation_buffers::num; ++b)
	{
		gpu_resource& buffer = m_anim_buffers.m_buffers[b];
		gpu_resource& staging = m_anim_buffers.m_staging[b];
		if (buffer.buffer_needs_realloc(bytesizes[b]))
		{
			release_if_valid(buffer);
			release_if_valid(staging);
			buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
				.type(gpu_resource::buffer)
				.bytestride(animation_buffers::bytestride(b))
				.bytesize(bytesizes[b])
				.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
			).claim();

			staging = gpu_resource::allocate(*m_device, gpu_resource::builder()
				.type(gpu_resource::buffer)
				.bytestride(animation_buffers::bytestride(b))
				.bytesize(bytesizes[b])
				.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
				.heap_type(D3D12_HEAP_TYPE_UPLOAD)
			).claim();
		}
	}

	// map buffer data to gpu
	map_resource<hlsl::animation_channel>(m_anim_buffers.m_staging[animation_buffers::channels], [this, &bytesizes](hlsl::animation_channel* dest) {
		memcpy(dest, m_anim_buffers.m_channels.data(), bytesizes[animation_buffers::channels]);
	});
	map_resource<hlsl::keyframe>(m_anim_buffers.m_staging[animation_buffers::keyframes], [this, &bytesizes](hlsl::keyframe* dest) {
		memcpy(dest, m_anim_buffers.m_keyframes.data(), bytesizes[animation_buffers::keyframes]);
	});

	m_anim_buffers.m_needs_upload = true;
}

void renderman::reallocate_image_texture(const contentman& cman, image_id id)
{
	if (id == k_id_invalid)
	{
		return;
	}

	const auto image_asset_res = cman.find_typed_asset<asset_type::image>(id);
	if (image_asset_res.is_fail())
	{
		return;
	}

	const image_asset& image_data = *image_asset_res.claim();
	unsigned char* raw_image_data = image_data.m_raw_data_ptr;
	if (raw_image_data == nullptr)
	{
		return;
	}

	// (re)allocate gpu texture (don't upload them yet)
	texture& target_tex = m_image_textures[id];
	if (!target_tex.m_staging_resource.is_valid() || !target_tex.m_gpu_resource.is_valid())
	{
		release_if_valid(target_tex.m_gpu_resource);
		release_if_valid(target_tex.m_staging_resource);

		target_tex.m_gpu_resource = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(image_data.m_pixel_width, image_data.m_pixel_height)
			.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
			.format(DXGI_FORMAT_R8G8B8A8_UNORM)
			).claim();

		const uint64 four_channel_bytesize = sizeof(uint8) * 4 * image_data.m_pixel_width * image_data.m_pixel_height;
		target_tex.m_staging_resource = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.buffer_single(four_channel_bytesize)
			.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
			.heap_type(D3D12_HEAP_TYPE_UPLOAD)
		).claim();

		// map the upload resource
		map_resource<uint8>(target_tex.m_staging_resource, [&image_data](uint8* dest)
		{
			for (uint64 p = 0u; p < image_data.calculate_num_pixels(); ++p)
				for (uint64 c = 0u; c < image_data.m_num_channels; ++c)
				{
					dest[(p * 4) + c] = image_data.get_pixel_channel_value(p, (uint32)c);
				}
		}).claim();

		target_tex.m_srv = create_resource_descriptor(target_tex.m_gpu_resource, descriptor::builder()
			.type(descriptor::srv)
		).claim();
	}
}

result<descriptor> renderman::create_resource_descriptor(
	dxresource& resource,
	const descriptor::builder& builder)
{
	using restype = result<descriptor>;
	restype result;

	const bool gpu_heap = builder.m_gpu_readable;
	const descriptor::type type = builder.m_type;
	if (gpu_heap && !descriptor::is_gpu_readable(type))
		return restype::make_fail("create_resource_descriptor(type, gpu_readable) failed > descriptor of type cannot be gpu_readable!");

	// allocate an entry on the target heap, and return the descriptor
	const descriptor_heap::slot heap_type = descriptor::dest_heap(type, gpu_heap);
	descheap& heap = get_descheap(heap_type);
	descriptor new_descriptor;
	new_descriptor.m_owner = heap_type;
	new_descriptor.m_type = type;
	new_descriptor.m_heap_idx = heap.allocate().claim();

	// now get the handles (cpu)
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
	heap.get_handles(new_descriptor.m_heap_idx, &cpu_handle).claim();

	D3D12_RESOURCE_DESC resource_desc = resource.GetDesc();
	DXGI_FORMAT format = resource_desc.Format;
	if (builder.m_format_override)
	{
		format = builder.m_format;
	}

	// now create the view using device
	switch (type)
	{
	case descriptor::rtv:
	{
		D3D12_RENDER_TARGET_VIEW_DESC view_desc{};
		view_desc.Format = format;
		switch (resource_desc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			view_desc.ViewDimension = D3D12_RTV_DIMENSION_BUFFER;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			view_desc.Texture2D.PlaneSlice = 0;
			view_desc.Texture2D.MipSlice = 0;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			return restype::make_fail("unsupported resource dimension!");
		}
		m_device->CreateRenderTargetView(&resource, &view_desc, cpu_handle);
	}break;
	case descriptor::dsv:
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC view_desc{};
		view_desc.Format = format;
		switch (resource_desc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			view_desc.Texture2D.MipSlice = 0;
			break;
		default:
			return restype::make_fail("unsupported resource dimension!");
		}
		m_device->CreateDepthStencilView(&resource, &view_desc, cpu_handle);
	}break;
	case descriptor::uav:
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC view_desc{};
		view_desc.Format = format;
		switch (resource_desc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			view_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			view_desc.Buffer.FirstElement = builder.m_first_element;
			view_desc.Buffer.NumElements = builder.m_num_elements;
			view_desc.Buffer.StructureByteStride = builder.m_bytestride;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			view_desc.Texture2D.MipSlice = 0;
			view_desc.Texture2D.PlaneSlice = 0;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			break;
		default:
			return restype::make_fail("unsupported resource dimension!");
		}
		m_device->CreateUnorderedAccessView(&resource, nullptr, &view_desc, cpu_handle);
	}break;
	case descriptor::cbv:
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC view_desc{};
		view_desc.BufferLocation = resource.GetGPUVirtualAddress();
		view_desc.SizeInBytes = (uint32)resource.GetDesc().Width;
		m_device->CreateConstantBufferView(&view_desc, cpu_handle);
	}break;
	case descriptor::srv:
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
		view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		view_desc.Format = format;
		switch (resource_desc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			view_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			view_desc.Buffer.FirstElement = builder.m_first_element;
			view_desc.Buffer.NumElements = builder.m_num_elements;
			view_desc.Buffer.StructureByteStride = builder.m_bytestride;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			view_desc.Texture2D.PlaneSlice = 0;
			view_desc.Texture2D.MipLevels = 1;
			view_desc.Texture2D.MostDetailedMip = 0;
			view_desc.Texture2D.ResourceMinLODClamp = 0.0f;
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			view_desc.ViewDimension = builder.m_view_as_array ? 
				D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE3D;
			break;
		default:
			return restype::make_fail("unsupported resource dimension!");
		}

		m_device->CreateShaderResourceView(&resource, &view_desc, cpu_handle);
	}break;
	case descriptor::sampler:
	{

	}break;
	}

	return new_descriptor;
}

result<> renderman::state_barrier(dxresource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	using restype = result<>;

	if (before == after)
	{
		return restype::make_warning("state_barrier: skipped - current state is the same as new state!");
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = {};
	barrier.Transition.pResource = &resource;
	barrier.Transition.StateAfter = after;
	barrier.Transition.StateBefore = before;
	barrier.Transition.Subresource = 0;
	m_cmdlist->ResourceBarrier(1, &barrier);
	return {};
}

result<> renderman::state_barrier(gpu_resource& resource, D3D12_RESOURCE_STATES after)
{
	using restype = result<>;
	if (!resource.is_valid())
		return restype::make_warning("state_barrier: skipped - invalid resource!");

	if (resource.m_current_state == after)
		return restype::make_warning("state_barrier: skipped - current state is the same as new state!");

	auto barrier_res = state_barrier(*resource.m_resource, resource.m_current_state, after);
	if (barrier_res.is_success())
	{
		resource.m_previous_state = resource.m_current_state;
		resource.m_current_state = after;
	}
	return barrier_res;
}

void renderman::clear_gpu_heaps()
{
	get_descheap(descriptor_heap::gpu_resource).m_stack_ptr = 0;
	get_descheap(descriptor_heap::gpu_sampler).m_stack_ptr = 0;
}

bool renderman::push_gpu_resource_descriptor(
	dxdevice& device,
	const descriptor& source_descriptor,
	uint32& out_gpu_heap_index,
	D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
	descheap& gpu_descheap = get_descheap(descriptor_heap::gpu_resource);
	dxdescheap* gpu_dx_heap = gpu_descheap.m_dxheap;

	// allocate on the gpu heap:
	uint32 slot = gpu_descheap.allocate().claim();

	// get the cpu handle of the new descriptor slot
	D3D12_CPU_DESCRIPTOR_HANDLE dest_cpu_handle;
	D3D12_GPU_DESCRIPTOR_HANDLE dest_gpu_handle;
	gpu_descheap.get_handles(slot, &dest_cpu_handle, &dest_gpu_handle).claim();

	// get the cpu handle of source
	const descheap& source_descheap = get_descheap(source_descriptor.m_owner);
	D3D12_CPU_DESCRIPTOR_HANDLE source_cpu_handle;
	source_descheap.get_handles(source_descriptor.m_heap_idx, &source_cpu_handle).claim();

	device.CopyDescriptorsSimple(1, dest_cpu_handle, source_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	if (out_gpu_handle)
	{
		*out_gpu_handle = dest_gpu_handle;
	}
	out_gpu_heap_index = slot;
	return true;
}

bool renderman::push_gpu_resource_descriptor(
	dxdevice& device,
	const descriptor& source_descriptor,
	gpu_optional& out_gpu_heap_index,
	D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
	uint32 out_index = 0u;
	if (push_gpu_resource_descriptor(device, source_descriptor, out_index, out_gpu_handle))
	{
		out_gpu_heap_index.set((int)out_index);
		return true;
	}
	else
	{
		out_gpu_heap_index.set_null();
		return false;
	}
}

result<> renderman::register_window(void* platform_handle)
{
	using restype = result<>;
	restype result;

	if (m_swapchain_lookup.contains(platform_handle))
	{
		return restype::make_warning("skipping register: window at handle already registered!");
	}

#if DF_WINDOWS
	RECT rect;
	uint2 dimensions = { 640, 480 };
	if (GetWindowRect((HWND)platform_handle, &rect))
	{
		dimensions.x = rect.right - rect.left;
		dimensions.y = rect.bottom - rect.top;
	}
#endif

	// make new swapchain
	m_swapchain_lookup[platform_handle] = (uint32)m_swapchains.size();
	m_swapchains.push_back({});
	{
		DXGI_SWAP_CHAIN_DESC1 scDesc = {};
		scDesc.BufferCount = k_num_swapchain_buffers;
		scDesc.Width = dimensions.x;
		scDesc.Height = dimensions.y;
		scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
		scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		scDesc.SampleDesc.Count = 1;

		HWND hwnd = (HWND)platform_handle;
		IDXGISwapChain1* temp_swapchain;
		if (!SUCCEEDED(m_factory->CreateSwapChainForHwnd(
			m_queue,
			hwnd,
			&scDesc,
			nullptr,
			nullptr,
			&temp_swapchain)))
		{
			return restype::make_fail("Factory::CreateSwapChainForHwnd() failed!");
		}

		m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

		swapchain& target_swapchain = m_swapchains.back();
		target_swapchain.m_swapchain = (dxswapchain*)temp_swapchain;
		target_swapchain.m_swapchain->GetSourceSize(&target_swapchain.m_current_size.x, &target_swapchain.m_current_size.y);

		// gather the resources, create the RTVS
		for (uint32 i = 0u; i < k_num_swapchain_buffers; ++i)
		{
			dxresource*& buffer = target_swapchain.m_buffers[i].m_resource;
			target_swapchain.m_swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer));
			target_swapchain.m_rtvs[i] = create_resource_descriptor(*buffer, descriptor::builder()
				.type(descriptor::rtv)
			).claim();
		}

		// make the depth resource
		target_swapchain.m_depth = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(dimensions.x, dimensions.y)
			.format(DXGI_FORMAT_D32_FLOAT)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
			.init_state(D3D12_RESOURCE_STATE_DEPTH_WRITE)
		).claim();
		target_swapchain.m_dsv = create_resource_descriptor(target_swapchain.m_depth, descriptor::builder()
			.type(descriptor::dsv)
		).claim();

		// make uav proxy
		target_swapchain.m_uav_proxy_resource = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.texture2D(dimensions.x, dimensions.y)
			.format(DXGI_FORMAT_R8G8B8A8_UNORM)
			.create_flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
		).claim();

		target_swapchain.m_uav_proxy = create_resource_descriptor(target_swapchain.m_uav_proxy_resource, descriptor::builder()
			.type(descriptor::uav)
		).claim();
	}

	return {};
}

result<gpu_resource*> renderman::gpu_resource_with_fallback(gpu_resource& resource)
{
	using restype = result<gpu_resource*>;
	if (resource.is_valid())
	{
		return &resource;
	}

	switch (resource.get_type())
	{
	case gpu_resource::type::buffer: return &m_dummies.m_buffer;
	}
	return restype::make_fail("resource type has no fallback!");
}

void renderscene::mesh_instance::apply_material(const contentman& cman, const mat_id mat)
{
	auto material = cman.find_material(mat);
	if (material)
	{
		m_color = material->m_basecolor;
		if (material->m_tex_basecolor != k_id_invalid)
			m_img_basecolor = material->m_tex_basecolor;
	}
}

const renderscene::line_builder& renderscene::line_builder::add_sphere(const transform& transform, const sphere& sphere, const float4& color) const
{
	constexpr float PI = 3.14159265359f;

	// Latitude rings
	const uint32 rings = 12;
	const uint32 segments = 12;
	const uint32 num_lines = (2 * rings - 1) * segments;
	m_owner.m_line_instances.reserve(m_owner.m_line_instances.size() + num_lines);

	const float radius = sphere.radius();
	const float3 position = sphere.position();

	float4 p0, p1;
	for (uint32 i = 1; i < rings; ++i)
	{
		float phi = PI * i / rings;
		float y = std::cos(phi) * radius;
		float ringRadius = std::sin(phi) * radius;
		for (uint32 j = 0; j < segments; ++j)
		{
			float a0 = 2.0f * PI * j / segments;
			float a1 = 2.0f * PI * (j + 1) / segments;

			const float3 p0n = position + float3(ringRadius * std::cos(a0), y, ringRadius * std::sin(a0));
			const float3 p1n = position + float3(ringRadius * std::cos(a1), y, ringRadius* std::sin(a1));
			p0 = transform.m_matrix * float4{ p0n, 1.0f };
			p1 = transform.m_matrix * float4{ p1n, 1.0f };
			add_line(p0, p1, color);
		}
	}
	for (uint32 j = 0; j < segments; ++j) {
		float theta0 = 2.0f * PI * j / segments;
		float theta1 = 2.0f * PI * (j + 1) / segments;
		for (uint32 i = 0; i < rings; ++i) {
			float phi0 = PI * i / rings;
			float phi1 = PI * (i + 1) / rings;

			const float3 p0n = position + float3(radius * std::sin(phi0) * std::cos(theta0), radius * std::cos(phi0), radius * std::sin(phi0) * std::sin(theta0));
			const float3 p1n = position + float3(radius * std::sin(phi1) * std::cos(theta0), radius * std::cos(phi1), radius * std::sin(phi1) * std::sin(theta0));
			p0 = transform.m_matrix * float4{ p0n, 1.0f};
			p1 = transform.m_matrix * float4{ p1n, 1.0f};
			add_line(p0, p1, color);
		}
	}
	return *this;
}

const renderscene::line_builder& renderscene::line_builder::add_box(const transform& transform, const box& box, const float4& color) const
{
	const float3 corners[8] =
	{
		{ box.abs_min().x, box.abs_min().y, box.abs_min().z },
		{ box.abs_max().x, box.abs_min().y, box.abs_min().z },
		{ box.abs_max().x, box.abs_max().y, box.abs_min().z },
		{ box.abs_min().x, box.abs_max().y, box.abs_min().z },
		{ box.abs_min().x, box.abs_min().y, box.abs_max().z },
		{ box.abs_max().x, box.abs_min().y, box.abs_max().z },
		{ box.abs_max().x, box.abs_max().y, box.abs_max().z },
		{ box.abs_min().x, box.abs_max().y, box.abs_max().z },
	};
	constexpr uint32 edges[][2] =
	{
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // bottom
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // top
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // sides
	};

	m_owner.m_line_instances.reserve(m_owner.m_line_instances.size() + 12);
	for (const auto& [a, b] : edges)
	{
		add_line(transform.m_matrix * float4(corners[a], 1), transform.m_matrix * float4(corners[b], 1), color);
	}
	return *this;
}

const renderscene::line_builder& renderscene::line_builder::add_transform(const transform& transform) const
{
	m_owner.m_line_instances.reserve(m_owner.m_line_instances.size() + 3);
	
	add_line(transform.m_matrix * float4(0,0,0,1), transform.m_matrix * float4(1, 0, 0, 1), float4(1,0,0,1));
	add_line(transform.m_matrix * float4(0,0,0,1), transform.m_matrix * float4(0, 1, 0, 1), float4(0,1,0,1));
	add_line(transform.m_matrix * float4(0,0,0,1), transform.m_matrix * float4(0, 0, 1, 1), float4(0,0,1,1));
	return *this;
}

void renderman::cbuffers::ensure_allocated(renderman& owner, cbuffer::slot slot, uint32 idx)
{
	if (idx >= m_resources[slot].size())
	{
		m_resources[slot].resize(idx + 1);
		m_cbvs[slot].resize(idx + 1);
	}

	gpu_resource& resource = m_resources[slot][idx];
	descriptor& descr = m_cbvs[slot][idx];
	if (!resource.is_valid())
	{
		resource = gpu_resource::allocate(*owner.m_device, gpu_resource::builder()
			.buffer_single(cbuffer::get_bytesize(slot))
			.init_state(D3D12_RESOURCE_STATE_GENERIC_READ)
			.heap_type(D3D12_HEAP_TYPE_UPLOAD)
			.cbuffer()
		).claim();

		descr = owner.create_resource_descriptor(resource, descriptor::builder()
			.type(descriptor::cbv)
		).claim();
	}
}

uint32 renderman::cbuffers::num(cbuffer::slot slot) const
{
	return (uint32)m_resources[slot].size();
}

gpu_resource& renderman::cbuffers::resource(cbuffer::slot slot, uint32 idx)
{
	return m_resources[slot][idx];
}

descriptor* renderman::cbuffers::cbv(cbuffer::slot slot, uint32 idx)
{
	return &m_cbvs[slot][idx];
}
}