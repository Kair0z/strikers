#include "renderman.h"
#include "contentman.h"
#include "logman.h"

static const char* k_vs_target = "vs_6_8";
static const char* k_ps_target = "ps_6_8";
static const char* k_cs_target = "cs_6_8";
static const char* k_ui_shader_filepath			= DF_FOLDER_SHADERS "ui.hlsl";
static const char* k_shading_shader_filepath	= DF_FOLDER_SHADERS "shading.hlsl";
static const char* k_skinning_shader_filepath	= DF_FOLDER_SHADERS "skinning.hlsl";
static const char* k_lines_shader_filepath		= DF_FOLDER_SHADERS "lines.hlsl";

namespace strikers {
static uint64 calculate_format_bytesize(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT: 
		return sizeof(float) * 1;
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT: 
		return sizeof(float) * 2;
	case DXGI_FORMAT_R32G32B32_FLOAT: 
	case DXGI_FORMAT_R32G32B32_UINT: 
		return sizeof(float) * 3;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
		return sizeof(float) * 4;
	}
	return 0;
}

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

static result<dxresource*> allocate_gpu_buffer(
	dxdevice& device,
	const uint64 bytesize,
	const uint64 bytestride,
	bool allow_uav,
	D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_COMMON,
	D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT)
{
	using restype = result<dxresource*>;

	D3D12_HEAP_PROPERTIES heap_props{};
	heap_props.Type = heap_type;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = bytesize;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	if (allow_uav) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
	{
		init_state = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	dxresource* resource;
	HRESULT hres = device.CreateCommittedResource(
		&heap_props,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		init_state,
		nullptr,
		IID_PPV_ARGS(&resource)
	);

	if (!SUCCEEDED(hres))
	{
		return restype::make_fail("");
	}
	else return restype::make_success(resource);
}

static result<dxresource*> allocate_gpu_texture(
	dxdevice& device,
	const uint32 num_dimensions,
	const uint3& dimensions,
	DXGI_FORMAT format,
	D3D12_RESOURCE_FLAGS allow_flags = {},
	D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_COMMON,
	D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT)
{
	using restype = result<dxresource*>;

	D3D12_HEAP_PROPERTIES heap_props{};
	heap_props.Type = heap_type;

	D3D12_RESOURCE_DESC desc = {};
	switch (num_dimensions)
	{
	case 1: desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D; break;
	case 2: desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; break;
	case 3: desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D; break;
	default:
		return restype::make_fail("texture of dimensions !1, !2 or !3 are not supported");
	}
	desc.Format = format;
	desc.Width = dimensions.x;
	desc.Height = dimensions.y;
	desc.DepthOrArraySize = dimensions.z;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	// desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = allow_flags;

	dxresource* resource;
	HRESULT hres = device.CreateCommittedResource(
		&heap_props,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		init_state,
		nullptr,
		IID_PPV_ARGS(&resource)
	);

	if (!SUCCEEDED(hres))
	{
		return restype::make_fail("");
	}
	else return resource;
}

static void release_if_valid(IUnknown* anything)
{
	if (anything != nullptr)
	{
		anything->Release();
	}
}

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
			"-Zi",
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

void renderman::compile_shaders()
{
	static bool once = true;
	if (!once) return;
	once = false;

	using restype = result<>;

	// shading shaders
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/shading.hlsl", "main_vs",
		k_vs_target,*m_device).claim();
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/shading.hlsl", "main_ps",
		k_ps_target, *m_device).claim();
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/ui.hlsl", "main_vs",
		k_vs_target, *m_device).claim();
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/ui.hlsl", "main_ps",
		k_ps_target, *m_device).claim();
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/skinning.hlsl", "main_cs",
		k_cs_target, *m_device).claim();
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/lines.hlsl", "main_vs",
		k_vs_target, *m_device).claim();
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/lines.hlsl", "main_ps",
		k_ps_target, *m_device).claim();
	
	compile_pipelines();
}

void renderman::populate_vertex_shader_input(pipeline_desc& pipeline)
{
	uint32 input_elements_byteoffset = 0u;
	auto push_element = [&input_elements_byteoffset, &pipeline](DXGI_FORMAT format, const char* semantic_name, uint32 semantic_idx = 0)
	{
		pipeline.m_input_elements.push_back({});
		pipeline.m_input_elements.back().AlignedByteOffset = input_elements_byteoffset;
		pipeline.m_input_elements.back().Format = format;
		pipeline.m_input_elements.back().InputSlot = 0u;
		pipeline.m_input_elements.back().InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		pipeline.m_input_elements.back().InstanceDataStepRate = 0;
		pipeline.m_input_elements.back().SemanticIndex = semantic_idx;
		pipeline.m_input_elements.back().SemanticName = semantic_name;
		input_elements_byteoffset += (uint32)calculate_format_bytesize(format);
	};
	push_element(DXGI_FORMAT_R32G32B32_FLOAT, "SV_POSITION");
	push_element(DXGI_FORMAT_R32G32B32_FLOAT, "NORMAL");
	push_element(DXGI_FORMAT_R32G32_FLOAT, "TEXCOORD", 0);
	push_element(DXGI_FORMAT_R32G32B32A32_UINT, "BLENDINDICES", 0);
	push_element(DXGI_FORMAT_R32G32B32A32_FLOAT, "BLENDWEIGHT", 0);
}

void renderman::compile_pipelines()
{
	using restype = result<>;

	m_pipelines.resize(pip_num + cpip_num);

	// configure skinning pipeline
	{
		pipeline_desc& compute_pipeline = m_pipelines[cpip_skinning];
		compute_pipeline.m_shaders_filepath = k_skinning_shader_filepath;
		compute_pipeline.m_cs_entrypoint = "main_cs";
		compute_pipeline.m_cs_target = k_cs_target;
		compute_pipeline.m_compute_desc.CS;
	}

	// configure shading pipeline
	{
		pipeline_desc& shading_pipeline = m_pipelines[pip_shading];
		shading_pipeline.m_shaders_filepath = k_shading_shader_filepath;
		shading_pipeline.m_ps_entrypoint = "main_ps";
		shading_pipeline.m_vs_entrypoint = "main_vs";
		shading_pipeline.m_ps_target = k_ps_target;
		shading_pipeline.m_vs_target = k_vs_target;
		shading_pipeline.m_desc = {};

		populate_vertex_shader_input(shading_pipeline);
		shading_pipeline.m_desc.InputLayout.NumElements = (uint32)shading_pipeline.m_input_elements.size();
		shading_pipeline.m_desc.InputLayout.pInputElementDescs = shading_pipeline.m_input_elements.data();
		shading_pipeline.m_desc.BlendState.AlphaToCoverageEnable = false;
		shading_pipeline.m_desc.BlendState.IndependentBlendEnable = false;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		shading_pipeline.m_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.DepthEnable = true;
		shading_pipeline.m_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		shading_pipeline.m_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		shading_pipeline.m_desc.DepthStencilState.StencilEnable = false;
		shading_pipeline.m_desc.DepthStencilState.StencilReadMask = 0;
		shading_pipeline.m_desc.DepthStencilState.StencilWriteMask = 0;
		shading_pipeline.m_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		shading_pipeline.m_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		shading_pipeline.m_desc.RasterizerState.AntialiasedLineEnable = false;
		shading_pipeline.m_desc.RasterizerState.ConservativeRaster;
		shading_pipeline.m_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		shading_pipeline.m_desc.RasterizerState.DepthBias = 0;
		shading_pipeline.m_desc.RasterizerState.DepthBiasClamp = 0;
		shading_pipeline.m_desc.RasterizerState.DepthClipEnable = false;
		shading_pipeline.m_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		shading_pipeline.m_desc.RasterizerState.ForcedSampleCount;
		shading_pipeline.m_desc.RasterizerState.FrontCounterClockwise = false;
		shading_pipeline.m_desc.RasterizerState.MultisampleEnable;
		shading_pipeline.m_desc.RasterizerState.SlopeScaledDepthBias;
		shading_pipeline.m_desc.SampleDesc.Count = 1;
		shading_pipeline.m_desc.SampleDesc.Quality = 0;
		shading_pipeline.m_desc.SampleMask = 0xFFFFFFFF;
		shading_pipeline.m_desc.NumRenderTargets = 1;		
		for (uint32 i = 0u; i < shading_pipeline.m_desc.NumRenderTargets; ++i)
		{
			shading_pipeline.m_desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].BlendEnable = TRUE;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].RenderTargetWriteMask = 0xf;
		}
	}
	// configure shading pipeline
	{
		pipeline_desc& shading_pipeline = m_pipelines[pip_wireframe];
		shading_pipeline.m_shaders_filepath = k_shading_shader_filepath;
		shading_pipeline.m_ps_entrypoint = "main_ps";
		shading_pipeline.m_vs_entrypoint = "main_vs";
		shading_pipeline.m_ps_target = k_ps_target;
		shading_pipeline.m_vs_target = k_vs_target;
		shading_pipeline.m_desc = {};

		populate_vertex_shader_input(shading_pipeline);
		shading_pipeline.m_desc.InputLayout.NumElements = (uint32)shading_pipeline.m_input_elements.size();
		shading_pipeline.m_desc.InputLayout.pInputElementDescs = shading_pipeline.m_input_elements.data();
		shading_pipeline.m_desc.BlendState.AlphaToCoverageEnable = false;
		shading_pipeline.m_desc.BlendState.IndependentBlendEnable = false;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		shading_pipeline.m_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		shading_pipeline.m_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		shading_pipeline.m_desc.DepthStencilState.DepthEnable = true;
		shading_pipeline.m_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		shading_pipeline.m_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		shading_pipeline.m_desc.DepthStencilState.StencilEnable = false;
		shading_pipeline.m_desc.DepthStencilState.StencilReadMask = 0;
		shading_pipeline.m_desc.DepthStencilState.StencilWriteMask = 0;
		shading_pipeline.m_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		shading_pipeline.m_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		shading_pipeline.m_desc.RasterizerState.AntialiasedLineEnable = false;
		shading_pipeline.m_desc.RasterizerState.ConservativeRaster;
		shading_pipeline.m_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		shading_pipeline.m_desc.RasterizerState.DepthBias = 0;
		shading_pipeline.m_desc.RasterizerState.DepthBiasClamp = 0;
		shading_pipeline.m_desc.RasterizerState.DepthClipEnable = false;
		shading_pipeline.m_desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		shading_pipeline.m_desc.RasterizerState.ForcedSampleCount;
		shading_pipeline.m_desc.RasterizerState.FrontCounterClockwise = false;
		shading_pipeline.m_desc.RasterizerState.MultisampleEnable;
		shading_pipeline.m_desc.RasterizerState.SlopeScaledDepthBias;
		shading_pipeline.m_desc.SampleDesc.Count = 1;
		shading_pipeline.m_desc.SampleDesc.Quality = 0;
		shading_pipeline.m_desc.SampleMask = 0xFFFFFFFF;
		shading_pipeline.m_desc.NumRenderTargets = 1;
		for (uint32 i = 0u; i < shading_pipeline.m_desc.NumRenderTargets; ++i)
		{
			shading_pipeline.m_desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].BlendEnable = TRUE;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
			shading_pipeline.m_desc.BlendState.RenderTarget[i].RenderTargetWriteMask = 0xf;
		}
	}
	// configure ui pipeline
	{
		pipeline_desc& ui_pipeline = m_pipelines[pip_ui];
		ui_pipeline.m_shaders_filepath = k_ui_shader_filepath;
		ui_pipeline.m_ps_entrypoint = "main_ps";
		ui_pipeline.m_vs_entrypoint = "main_vs";
		ui_pipeline.m_ps_target = k_ps_target;
		ui_pipeline.m_vs_target = k_vs_target;
		ui_pipeline.m_desc = {};

		ui_pipeline.m_desc.InputLayout.NumElements = (uint32)ui_pipeline.m_input_elements.size();
		ui_pipeline.m_desc.InputLayout.pInputElementDescs = ui_pipeline.m_input_elements.data();
		ui_pipeline.m_desc.BlendState.AlphaToCoverageEnable = false;
		ui_pipeline.m_desc.BlendState.IndependentBlendEnable = false;
		ui_pipeline.m_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		ui_pipeline.m_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		ui_pipeline.m_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		ui_pipeline.m_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		ui_pipeline.m_desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		ui_pipeline.m_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		ui_pipeline.m_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		ui_pipeline.m_desc.DepthStencilState.DepthEnable = true;
		ui_pipeline.m_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		ui_pipeline.m_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		ui_pipeline.m_desc.DepthStencilState.StencilEnable = false;
		ui_pipeline.m_desc.DepthStencilState.StencilReadMask = 0;
		ui_pipeline.m_desc.DepthStencilState.StencilWriteMask = 0;
		ui_pipeline.m_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		ui_pipeline.m_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		ui_pipeline.m_desc.RasterizerState.AntialiasedLineEnable = false;
		ui_pipeline.m_desc.RasterizerState.ConservativeRaster;
		ui_pipeline.m_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		ui_pipeline.m_desc.RasterizerState.DepthBias = 0;
		ui_pipeline.m_desc.RasterizerState.DepthBiasClamp = 0;
		ui_pipeline.m_desc.RasterizerState.DepthClipEnable = false;
		ui_pipeline.m_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		ui_pipeline.m_desc.RasterizerState.ForcedSampleCount;
		ui_pipeline.m_desc.RasterizerState.FrontCounterClockwise = false;
		ui_pipeline.m_desc.RasterizerState.MultisampleEnable;
		ui_pipeline.m_desc.RasterizerState.SlopeScaledDepthBias;
		ui_pipeline.m_desc.SampleDesc.Count = 1;
		ui_pipeline.m_desc.SampleDesc.Quality = 0;
		ui_pipeline.m_desc.SampleMask = 0xFFFFFFFF;
		ui_pipeline.m_desc.NumRenderTargets = 1;
		for (uint32 i = 0u; i < ui_pipeline.m_desc.NumRenderTargets; ++i)
		{
			ui_pipeline.m_desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].BlendEnable = TRUE;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
			ui_pipeline.m_desc.BlendState.RenderTarget[i].RenderTargetWriteMask = 0xf;
		}
	}
	// configure lines pipeline
	{
		pipeline_desc& pipeline = m_pipelines[pip_lines];
		pipeline.m_shaders_filepath = k_lines_shader_filepath;
		pipeline.m_ps_entrypoint = "main_ps";
		pipeline.m_vs_entrypoint = "main_vs";
		pipeline.m_ps_target = k_ps_target;
		pipeline.m_vs_target = k_vs_target;
		pipeline.m_desc = {};

		pipeline.m_desc.InputLayout.NumElements = (uint32)pipeline.m_input_elements.size();
		pipeline.m_desc.InputLayout.pInputElementDescs = pipeline.m_input_elements.data();
		pipeline.m_desc.BlendState.AlphaToCoverageEnable = false;
		pipeline.m_desc.BlendState.IndependentBlendEnable = false;
		pipeline.m_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		pipeline.m_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		pipeline.m_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		pipeline.m_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		pipeline.m_desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		pipeline.m_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		pipeline.m_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		pipeline.m_desc.DepthStencilState.DepthEnable = true;
		pipeline.m_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pipeline.m_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pipeline.m_desc.DepthStencilState.StencilEnable = false;
		pipeline.m_desc.DepthStencilState.StencilReadMask = 0;
		pipeline.m_desc.DepthStencilState.StencilWriteMask = 0;
		pipeline.m_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pipeline.m_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		pipeline.m_desc.RasterizerState.AntialiasedLineEnable = false;
		pipeline.m_desc.RasterizerState.ConservativeRaster;
		pipeline.m_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pipeline.m_desc.RasterizerState.DepthBias = 0;
		pipeline.m_desc.RasterizerState.DepthBiasClamp = 0;
		pipeline.m_desc.RasterizerState.DepthClipEnable = false;
		pipeline.m_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pipeline.m_desc.RasterizerState.ForcedSampleCount;
		pipeline.m_desc.RasterizerState.FrontCounterClockwise = false;
		pipeline.m_desc.RasterizerState.MultisampleEnable;
		pipeline.m_desc.RasterizerState.SlopeScaledDepthBias;
		pipeline.m_desc.SampleDesc.Count = 1;
		pipeline.m_desc.SampleDesc.Quality = 0;
		pipeline.m_desc.SampleMask = 0xFFFFFFFF;
		pipeline.m_desc.NumRenderTargets = 1;
		for (uint32 i = 0u; i < pipeline.m_desc.NumRenderTargets; ++i)
		{
			pipeline.m_desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
			pipeline.m_desc.BlendState.RenderTarget[i].BlendEnable = TRUE;
			pipeline.m_desc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
			pipeline.m_desc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			pipeline.m_desc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			pipeline.m_desc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
			pipeline.m_desc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
			pipeline.m_desc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			pipeline.m_desc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			pipeline.m_desc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
			pipeline.m_desc.BlendState.RenderTarget[i].RenderTargetWriteMask = 0xf;
		}
	}
	// here we create the actual dx12 stuff
	for (uint32 i = 0u; i < m_pipelines.size(); ++i)
	{
		pipeline_desc& pip = m_pipelines[i];
		if (is_pipeline_compute((pipeline)i))
		{
			const auto& cs_shader = m_shaderman.get_shader(
				pip.m_shaders_filepath, pip.m_cs_entrypoint, pip.m_cs_target).claim();
			
			D3D12_COMPUTE_PIPELINE_STATE_DESC& pipeline_desc = pip.m_compute_desc;
			pipeline_desc.CS = cs_shader->m_shader_bytecode;
			pipeline_desc.pRootSignature = cs_shader->m_signature;
			pip.m_dxsignature = cs_shader->m_signature;
			HRESULT hres = m_device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pip.m_dxpipeline));
			if (!SUCCEEDED(hres))
			{
				int a = 0; // todo: do something...
			}
		}
		else if (is_pipeline_graphics((pipeline)i))
		{
			const auto& vs_shader = m_shaderman.get_shader(
				pip.m_shaders_filepath, pip.m_vs_entrypoint, pip.m_vs_target).claim();
			const auto& ps_shader = m_shaderman.get_shader(
				pip.m_shaders_filepath, pip.m_ps_entrypoint, pip.m_ps_target).claim();

			D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipeline_desc = pip.m_desc;
			pipeline_desc.PS = ps_shader->m_shader_bytecode;
			pipeline_desc.VS = vs_shader->m_shader_bytecode;
			pipeline_desc.pRootSignature = vs_shader->m_signature;
			pip.m_dxsignature = vs_shader->m_signature;
			HRESULT hres = m_device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pip.m_dxpipeline));
			if (!SUCCEEDED(hres))
			{
				int a = 0; // todo: do something...
			}
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

	logman::log("renderman::initialize {}", 1.0f);

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
			D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE
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
		for (uint32 i = 0; i < k_num_descriptor_heaps; ++i)
		{
			const descriptor_heap heap = (descriptor_heap)i;

			const D3D12_DESCRIPTOR_HEAP_TYPE heap_type = get_descheap_type(heap);
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
			heap_desc.Type = heap_type;
			heap_desc.NumDescriptors = get_descheap_size(heap);
			heap_desc.Flags = (is_descheap_gpu_readable(heap) ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
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
	// constantbuffer
	{
		const uint64 constbuffer_bytesize = max(256, sizeof(gpu_cbuffer));
		m_constantbuffer = allocate_gpu_buffer(*m_device, constbuffer_bytesize, sizeof(constbuffer_bytesize), false, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD).claim();
		m_constantbuffer_cbv = create_resource_descriptor(*m_constantbuffer, descriptor_type::cbv, false).claim();
	}
	return result;
}

void renderman::render(renderscene& scene, const contentman& cman)
{
	process_scene(scene, cman);

	// wait for previous frame
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
	const descheap& dsv_heap = get_descheap(descriptor_heap::dsv);
	const uint2 backbuffer_size = swapchain.m_current_size;

	D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv_handle, backbuffer_dsv_handle;
	rtv_heap.get_handles(current_backbuffer_rtv.m_heap_idx, &backbuffer_rtv_handle).claim();
	dsv_heap.get_handles(swapchain.m_dsv.m_heap_idx, &backbuffer_dsv_handle).claim();

	m_cmd_allocator->Reset();
	m_cmdlist->Reset(m_cmd_allocator, nullptr);

	// clear & re-bind bindless resource heaps
	clear_gpu_heaps();
	dxdescheap* global_descheaps[]{
		get_descheap(descriptor_heap::gpu_resource).m_dxheap,
		get_descheap(descriptor_heap::gpu_sampler).m_dxheap
	};
	m_cmdlist->SetDescriptorHeaps(_countof(global_descheaps), global_descheaps);
	
	// upload pending textures & meshbuffers
	upload_buffers();
	
	cmd_transition_barrier(current_backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

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

	// update constant buffer
	map_resource<gpu_cbuffer>(*m_constantbuffer, [&scene, &viewport](gpu_cbuffer* cbuffer)
	{
		const auto mat_view = calculate_view_mat(scene.m_camera.m_transform.m_matrix);
		const auto mat_proj = calculate_proj_mat(scene.m_camera.m_fov, (float)viewport.Width / (float)viewport.Height, scene.m_camera.m_near, scene.m_camera.m_far);
		cbuffer->m_viewprojection = mat_proj * mat_view;
		cbuffer->m_light_color = scene.m_light.m_color;
		cbuffer->m_light_direction = scene.m_light.m_direction;
	});

	// shading passes: pip_shading | pip_wireframe
	if (!scene.m_mesh_instances.empty() && !scene.m_batch_instance_lookup.empty())
	{
		// update instance buffer
		map_resource<gpu_instance>(*m_instancebuffer, [this, &scene](gpu_instance* instance)
		{
			for (const auto& pair : scene.m_batch_instance_lookup)
			{
				const auto& batch_key = pair.first;
				const vector<uint32>& batch_indices = pair.second;
				for (const uint32& instance_index : batch_indices)
				{
					const auto& instance_data = scene.m_mesh_instances[instance_index];
					(*instance).m_transform = instance_data.m_transform.m_matrix;
					(*instance).m_color = instance_data.m_color;

					const image_id tex_basecolor = instance_data.m_img_basecolor;
					if (m_image_textures.contains(tex_basecolor))
					{
						(*instance).m_tex_basecolor_idx = m_image_textures.at(tex_basecolor).m_gpu_heap_slot;
					}
					instance++;
				}
			}
		});

		static const uint32 k_num_shaders = (uint32)shader::num;
		static const pipeline k_pipelines[k_num_shaders]
		{
			pip_shading,
			pip_wireframe
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
			m_cmdlist->SetGraphicsRootConstantBufferView(0, m_constantbuffer->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer->GetGPUVirtualAddress());

			for (const auto& pair : scene.m_batch_instance_lookup)
			{
				const renderscene::batch_key& batch_key = pair.first;
				const mesh_id mesh = batch_key.m_mesh;
				const shader shdr = batch_key.m_shader;
				if (shdr != (shader)i)
				{
					continue;
				}

				if (!m_mesh_buffers.contains(mesh))
				{
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

				mesh_buffers.m_vtb_view.BufferLocation = mesh_buffers.m_vertbuffer->GetGPUVirtualAddress();
				mesh_buffers.m_vtb_view.SizeInBytes = (uint32)mesh_buffers.m_vertbuffer->GetDesc().Width;
				mesh_buffers.m_vtb_view.StrideInBytes = sizeof(gpu_vertex);
				mesh_buffers.m_idx_view.BufferLocation = mesh_buffers.m_indbuffer->GetGPUVirtualAddress();
				mesh_buffers.m_idx_view.Format = DXGI_FORMAT_R32_UINT;
				mesh_buffers.m_idx_view.SizeInBytes = (uint32)mesh_buffers.m_indbuffer->GetDesc().Width;

				dxresource const* indexbuffer = mesh_buffers.m_indbuffer;
				dxresource const* vertbuffer = mesh_buffers.m_vertbuffer;
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
	}

	// line passes: pip_lines
	if (!scene.m_line_instances.empty())
	{
		PIXScopedEvent(m_cmdlist, 0u, "lines");
		m_cmdlist->SetPipelineState(m_pipelines[pip_lines].m_dxpipeline);
		m_cmdlist->SetGraphicsRootSignature(m_pipelines[pip_lines].m_dxsignature);

		// update lines instance buffer
		const uint32 num_instances = (uint32)scene.m_line_instances.size();
		map_resource<gpu_line_instance>(*m_instancebuffer_lines, [this, &scene, num_instances](gpu_line_instance* instance)
		{
			memcpy(instance, scene.m_line_instances.data(), sizeof(gpu_line_instance) * num_instances);
		});

		m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
		m_cmdlist->SetGraphicsRootConstantBufferView(0, m_constantbuffer->GetGPUVirtualAddress());
		m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer_lines->GetGPUVirtualAddress());
		m_cmdlist->DrawInstanced(2, num_instances, 0, 0);
	}

	// ui passes: pip_ui
	if (!scene.m_ui_instances.empty())
	{
		PIXScopedEvent(m_cmdlist, 0u, "UI");

		m_cmdlist->SetPipelineState(m_pipelines[pip_ui].m_dxpipeline);
		m_cmdlist->SetGraphicsRootSignature(m_pipelines[pip_ui].m_dxsignature);

		// update instance buffer
		const uint32 num_instances = (uint32)scene.m_ui_instances.size();
		map_resource<gpu_ui_instance>(*m_instancebuffer_ui, [this, &scene, num_instances](gpu_ui_instance* instance)
		{
			memcpy(instance, scene.m_ui_instances.data(), sizeof(gpu_ui_instance) * num_instances);
			for (const auto& cpu_instance : scene.m_ui_instances)
			{
				if (m_image_textures.contains(cpu_instance.m_image))
				{
					instance->m_tex_heap_idx = m_image_textures.at(cpu_instance.m_image).m_gpu_heap_slot;
				}
				instance++;
			}
		});

		m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer_ui->GetGPUVirtualAddress());
		m_cmdlist->DrawInstanced(6, num_instances, 0, 0);
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

void renderman::process_scene(renderscene& scene, const contentman& cman)
{
	// process the scene
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
		if (m_instancebuffer_ui == nullptr || m_instancebuffer_ui->GetDesc().Width < ui_instancebuffer_bytesize)
		{
			release_if_valid(m_instancebuffer_ui);
			m_instancebuffer_ui = allocate_gpu_buffer(*m_device, ui_instancebuffer_bytesize, sizeof(gpu_ui_instance), false, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD).claim();

			descriptor_args srv_builder{};
			srv_builder.m_buffer_srv.NumElements = num_ui_instances;
			srv_builder.m_buffer_srv.StructureByteStride = sizeof(gpu_ui_instance);
			m_instancebuffer_ui_srv = create_resource_descriptor(*m_instancebuffer_ui, descriptor_type::srv, false, srv_builder).claim();
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

			reallocate_image_texture(cman, instance.m_img_basecolor);
			reallocate_skeleton_buffers(cman, instance.m_skeleton);

			// (re)allocate the mesh buffers
			if (!m_mesh_buffers.contains(instance.m_mesh))
			{
				const auto& mesh_asset = mesh_asset_res.claim();
				const uint64 vertbuffer_size = mesh_asset->get_num_vertices() * sizeof(gpu_vertex);
				const uint64 indbuffer_size = mesh_asset->m_indices.size() * sizeof(uint32);
				meshbuffers& meshbuffs = m_mesh_buffers[instance.m_mesh];
				meshbuffs.m_vertbuffer = allocate_gpu_buffer(*m_device, vertbuffer_size, sizeof(gpu_vertex), false, D3D12_RESOURCE_STATE_COPY_DEST).claim();
				meshbuffs.m_indbuffer = allocate_gpu_buffer(*m_device, indbuffer_size, sizeof(uint32), false, D3D12_RESOURCE_STATE_COPY_DEST).claim();
				meshbuffs.m_vertex_stagingbuffer = allocate_gpu_buffer(*m_device, vertbuffer_size, sizeof(gpu_vertex), false, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_HEAP_TYPE_UPLOAD).claim();
				meshbuffs.m_index_stagingbuffer = allocate_gpu_buffer(*m_device, indbuffer_size, sizeof(uint32), false, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_HEAP_TYPE_UPLOAD).claim();

				map_resource<uint32>(*meshbuffs.m_index_stagingbuffer, [&mesh_asset, indbuffer_size](uint32* index) {
					memcpy(index, mesh_asset->m_indices.data(), indbuffer_size);
				}).claim();

				map_resource<gpu_vertex>(*meshbuffs.m_vertex_stagingbuffer, [&mesh_asset](gpu_vertex* vertex) {
					for (uint32 i = 0u; i < mesh_asset->m_vertices.size(); ++i)
					{
						const mesh_asset::vertex& in_vertex = mesh_asset->m_vertices[i];
						(*vertex).m_position = in_vertex.m_position;
						(*vertex).m_normal = in_vertex.m_normal;
						(*vertex).m_uv = { in_vertex.m_uv.x, in_vertex.m_uv.y };
						(*vertex).m_bone_weights = in_vertex.m_bone_weights;
						(*vertex).m_bone_ids = in_vertex.m_bone_indices;
						++vertex;
					}
				}).claim();
			}
		}

		// (re) allocate the instance buffer
		const uint32 num_mesh_instances = (uint32)scene.m_mesh_instances.size();
		const uint64 instancebuffer_bytesize = sizeof(gpu_instance) * num_mesh_instances;
		if (m_instancebuffer == nullptr || m_instancebuffer->GetDesc().Width < instancebuffer_bytesize)
		{
			release_if_valid(m_instancebuffer);
			m_instancebuffer = allocate_gpu_buffer(*m_device, instancebuffer_bytesize, sizeof(gpu_instance), false, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD).claim();

			descriptor_args srv_builder{};
			srv_builder.m_buffer_srv.NumElements = num_mesh_instances;
			srv_builder.m_buffer_srv.StructureByteStride = sizeof(gpu_instance);
			m_instancebuffer_srv = create_resource_descriptor(*m_instancebuffer, descriptor_type::srv, false, srv_builder).claim();
		}
	}
	if (!scene.m_line_instances.empty())
	{
		const uint32 num_lines = (uint32)scene.m_line_instances.size();
		const uint64 bytesize = sizeof(gpu_line_instance) * num_lines;
		if (m_instancebuffer_lines == nullptr || m_instancebuffer_lines->GetDesc().Width < bytesize)
		{
			release_if_valid(m_instancebuffer_lines);
			m_instancebuffer_lines = allocate_gpu_buffer(*m_device, bytesize, sizeof(gpu_line_instance), false, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_UPLOAD).claim();

			descriptor_args srv_builder{};
			srv_builder.m_buffer_srv.NumElements = num_lines;
			srv_builder.m_buffer_srv.StructureByteStride = sizeof(gpu_line_instance);
			m_instancebuffer_lines_srv = create_resource_descriptor(*m_instancebuffer_lines, descriptor_type::srv, false, srv_builder).claim();
		}
	}
}

void renderman::upload_buffers()
{
	for (auto& pair : m_mesh_buffers)
	{
		meshbuffers& buffers = pair.second;
		if (!buffers.m_uploaded)
		{
			m_cmdlist->CopyResource(buffers.m_vertbuffer, buffers.m_vertex_stagingbuffer);
			m_cmdlist->CopyResource(buffers.m_indbuffer, buffers.m_index_stagingbuffer);
			cmd_transition_barrier(*buffers.m_vertbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			cmd_transition_barrier(*buffers.m_indbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			buffers.m_uploaded = true;
		}
	}
	for (auto& pair : m_skel_buffers)
	{
		skeletonbuffers& buffers = pair.second;
		if (!buffers.m_uploaded)
		{
			m_cmdlist->CopyResource(buffers.m_bone_buffer, buffers.m_bone_buffer_staging);
			cmd_transition_barrier(*buffers.m_bone_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			buffers.m_uploaded = true;
		}
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
			const uint32 texture_width = (uint32)tex.m_gpu_resource->GetDesc().Width;
			const uint32 texture_height = (uint32)tex.m_gpu_resource->GetDesc().Height;

			D3D12_TEXTURE_COPY_LOCATION source{};
			source.PlacedFootprint.Offset = 0;
			source.PlacedFootprint.Footprint.Width = texture_width;
			source.PlacedFootprint.Footprint.Height = texture_height;
			source.PlacedFootprint.Footprint.Depth = 1;
			source.PlacedFootprint.Footprint.Format = tex.m_gpu_resource->GetDesc().Format;
			source.PlacedFootprint.Footprint.RowPitch = (uint32)(texture_width * pixel_bytesize);
			source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			source.pResource = tex.m_staging_resource;
			D3D12_TEXTURE_COPY_LOCATION dest{};
			dest.pResource = tex.m_gpu_resource;
			dest.SubresourceIndex = 0;
			dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			m_cmdlist->CopyTextureRegion(&dest, DstX, DstY, DstZ, &source, nullptr);
			cmd_transition_barrier(*tex.m_gpu_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			tex.m_uploaded = true;
		}

		// push each texture onto the gpu heap
		D3D12_GPU_DESCRIPTOR_HANDLE unused_gpu_handle;
		push_gpu_resource_descriptor(*m_device, tex.m_srv, tex.m_gpu_heap_slot, unused_gpu_handle);
	}
}

void renderman::reallocate_skeleton_buffers(const contentman& cman, skel_id id)
{
	if (id == k_id_invalid)
	{
		return;
	}

	const auto found_asset_res = cman.find_typed_asset<asset_type::skeleton>(id);
	if (found_asset_res.is_fail())
	{
		return;
	}

	const auto& skeleton_asset = found_asset_res.claim();
	const auto& bones = skeleton_asset->m_bones;
	const uint32 num_bones = (uint32)skeleton_asset->m_bones.size();
	const uint32 bytesize = (uint32)sizeof(gpu_bone) * num_bones;
	skeletonbuffers& buffers = m_skel_buffers[id];
	if (buffers.m_bone_buffer == nullptr)
	{
		release_if_valid(buffers.m_bone_buffer);
		release_if_valid(buffers.m_skinned_buffer);

		buffers.m_bone_buffer_staging = allocate_gpu_buffer(
			*m_device,
			bytesize,
			sizeof(gpu_bone),
			false,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_HEAP_TYPE_UPLOAD).claim();

		// map the upload resource
		map_resource<gpu_bone>(*buffers.m_bone_buffer_staging, [num_bones, &skeleton_asset](gpu_bone* dest)
		{
			for (uint32 i = 0u; i < num_bones; ++i)
			{
				dest[i].m_matrix = skeleton_asset->m_bones[i].m_offset_matrix;
				dest[i].m_parent = skeleton_asset->m_bones[i].m_parent_idx;
			}
		}).claim();

		buffers.m_bone_buffer = allocate_gpu_buffer(
			*m_device,
			bytesize,
			sizeof(gpu_bone),
			false,
			D3D12_RESOURCE_STATE_COPY_DEST).claim();

		buffers.m_skinned_buffer = allocate_gpu_buffer(
			*m_device,
			bytesize,
			sizeof(gpu_bone),
			false).claim();
	}
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
	if (target_tex.m_staging_resource == nullptr || target_tex.m_gpu_resource == nullptr)
	{
		release_if_valid(target_tex.m_gpu_resource);
		release_if_valid(target_tex.m_staging_resource);

		target_tex.m_gpu_resource = allocate_gpu_texture(
			*m_device,
			2u,
			{ image_data.m_pixel_width, image_data.m_pixel_height, 1 },
			DXGI_FORMAT_R8G8B8A8_UNORM,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COPY_DEST).claim();

		const uint64 four_channel_bytesize = sizeof(uint8) * 4 * image_data.m_pixel_width * image_data.m_pixel_height;
		target_tex.m_staging_resource = allocate_gpu_buffer(
			*m_device,
			four_channel_bytesize,
			sizeof(unsigned char),
			false,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_HEAP_TYPE_UPLOAD).claim();

		// map the upload resource
		map_resource<uint8>(*target_tex.m_staging_resource, [&image_data](uint8* dest)
		{
			for (uint64 p = 0u; p < image_data.calculate_num_pixels(); ++p)
				for (uint64 c = 0u; c < image_data.m_num_channels; ++c)
				{
					dest[(p * 4) + c] = image_data.get_pixel_channel_value(p, (uint32)c);
				}
		}).claim();

		target_tex.m_srv = create_resource_descriptor(
			*target_tex.m_gpu_resource, descriptor_type::srv, false).claim();
	}
}

result<renderman::descriptor> renderman::create_resource_descriptor(
	dxresource& resource,
	descriptor_type type,
	bool gpu_readable,
	const descriptor_args& args)
{
	using restype = result<descriptor>;
	restype result;

	if (gpu_readable && !can_gpu_read_descriptor(type))
		return restype::make_fail("create_resource_descriptor(type, gpu_readable) failed > descriptor of type cannot be gpu_readable!");

	// allocate an entry on the target heap, and return the descriptor
	const descriptor_heap heap_type = get_descheap_type(type, gpu_readable);
	descheap& heap = get_descheap(heap_type);
	descriptor new_descriptor;
	new_descriptor.m_owner = heap_type;
	new_descriptor.m_type = type;
	new_descriptor.m_heap_idx = heap.allocate().claim();

	// now get the handles (cpu)
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
	heap.get_handles(new_descriptor.m_heap_idx, &cpu_handle).claim();

	// now create the view using device
	D3D12_RESOURCE_DESC resource_desc = resource.GetDesc();
	switch (type)
	{
	case descriptor_type::rtv:
	{
		D3D12_RENDER_TARGET_VIEW_DESC view_desc{};
		view_desc.Format = resource_desc.Format;
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
	case descriptor_type::dsv:
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC view_desc{};
		view_desc.Format = resource_desc.Format;
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
	case descriptor_type::uav:
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC view_desc{};
		view_desc.Format = resource_desc.Format;
		switch (resource_desc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			view_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
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
	case descriptor_type::cbv:
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC view_desc{};
		view_desc.BufferLocation = resource.GetGPUVirtualAddress();
		view_desc.SizeInBytes = (uint32)resource.GetDesc().Width;
		m_device->CreateConstantBufferView(&view_desc, cpu_handle);
	}break;
	case descriptor_type::srv:
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
		view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		view_desc.Format = resource_desc.Format;
		switch (resource_desc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			view_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			view_desc.Buffer = args.m_buffer_srv;
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
			view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			break;
		default:
			return restype::make_fail("unsupported resource dimension!");
		}
		m_device->CreateShaderResourceView(&resource, &view_desc, cpu_handle);
	}break;
	case descriptor_type::sampler:
	{

	}break;
	}

	return new_descriptor;
}

void renderman::cmd_transition_barrier(dxresource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = {};
	barrier.Transition.pResource = &resource;
	barrier.Transition.StateAfter = after;
	barrier.Transition.StateBefore = before;
	barrier.Transition.Subresource = 0;
	m_cmdlist->ResourceBarrier(1, &barrier);
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
	D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu_handle)
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
	
	out_gpu_handle = dest_gpu_handle;
	out_gpu_heap_index = slot;
	return true;
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
			dxresource*& buffer = target_swapchain.m_buffers[i];
			target_swapchain.m_swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer));
			target_swapchain.m_rtvs[i] = create_resource_descriptor(*buffer, descriptor_type::rtv, false).claim();
		}

		// make the depth resource
		target_swapchain.m_depth = allocate_gpu_texture(
			*m_device,
			2,
			{ dimensions.x, dimensions.y, 1 },
			DXGI_FORMAT_D32_FLOAT,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
			D3D12_RESOURCE_STATE_DEPTH_WRITE).claim();
		target_swapchain.m_dsv = create_resource_descriptor(*target_swapchain.m_depth, descriptor_type::dsv, false).claim();

		// make uav proxy
		target_swapchain.m_uav_proxy_resource = allocate_gpu_texture(
			*m_device,
			2,
			{ dimensions.x, dimensions.y, 1 },
			DXGI_FORMAT_R8G8B8A8_UNORM,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE).claim();

		target_swapchain.m_uav_proxy = create_resource_descriptor(*target_swapchain.m_uav_proxy_resource, descriptor_type::uav, false).claim();
	}

	return {};
}

void renderscene::mesh_instance::apply_material(const contentman& cman, const mat_id mat)
{
	auto material = cman.find_material(mat);
	if (material && material->m_tex_basecolor != k_id_invalid)
	{
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
}