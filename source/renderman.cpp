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
static const char* k_lines_shader_filepath		= DF_FOLDER_SHADERS "lines.hlsl";

static const char* k_vsps_entrypoints[]{ k_vs_entry, k_ps_entry };
static const char* k_vsps_targets[] { k_vs_target, k_ps_target };
static const char* k_vsps_shaders[] {
	k_ui_shader_filepath,
	k_shading_shader_filepath,
	k_lines_shader_filepath
};
static const char* k_vs_shaders[]{
	k_shadows_shader_filepath
};

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

	// vsps shaders
	for (uint32 i = 0u; i < _countof(k_vsps_shaders); ++i)
	{
		for (uint32 s = 0u; s < _countof(k_vsps_entrypoints); ++s)
		{
			m_shaderman.compile_shader(k_vsps_shaders[i], k_vsps_entrypoints[s], k_vsps_targets[s], *m_device).claim();
		}
	}
	for (uint32 i = 0u; i < _countof(k_vs_shaders); ++i)
	{
		m_shaderman.compile_shader(k_vs_shaders[i], k_vsps_entrypoints[0], k_vsps_targets[0], *m_device).claim();
	}

	// cs shaders
	m_shaderman.compile_shader("D:/Git/strikers/hlsl/skinning.hlsl", "main_cs",
		k_cs_target, *m_device).claim();
	
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
		compute_pipeline.m_cs_entrypoint = k_cs_entry;
		compute_pipeline.m_cs_target = k_cs_target;
		compute_pipeline.m_compute_desc.CS;
	}

	// configure shading pipeline
	{
		pipeline_desc& shading_pipeline = m_pipelines[pip_shading];
		shading_pipeline.m_shaders_filepath = k_shading_shader_filepath;
		shading_pipeline.m_ps_entrypoint = k_ps_entry;
		shading_pipeline.m_vs_entrypoint = k_vs_entry;
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
	// configure wireframe pipeline
	{
		pipeline_desc& shading_pipeline = m_pipelines[pip_wireframe];
		shading_pipeline.m_shaders_filepath = k_shading_shader_filepath;
		shading_pipeline.m_ps_entrypoint = k_ps_entry;
		shading_pipeline.m_vs_entrypoint = k_vs_entry;
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
		ui_pipeline.m_ps_entrypoint = k_ps_entry;
		ui_pipeline.m_vs_entrypoint = k_vs_entry;
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
		pipeline.m_ps_entrypoint = k_ps_entry;
		pipeline.m_vs_entrypoint = k_vs_entry;
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
	// configure shadows pipeline
	{
		pipeline_desc& pipeline = m_pipelines[pip_shadows];
		pipeline.m_shaders_filepath = k_shadows_shader_filepath;
		pipeline.m_vs_entrypoint = k_vs_entry;
		pipeline.m_vs_target = k_vs_target;
		pipeline.m_ps_entrypoint = ""; // no pixel shader
		pipeline.m_ps_target = ""; // no pixel shader
		pipeline.m_desc = {};

		populate_vertex_shader_input(pipeline);

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
		pipeline.m_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
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

			shaderman::shader_entry const* pixelshader = nullptr;
			if (!pip.m_ps_entrypoint.empty() && !pip.m_ps_target.empty())
			{
				pixelshader = m_shaderman.get_shader(
					pip.m_shaders_filepath, pip.m_ps_entrypoint, pip.m_ps_target).claim();
			}

			D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipeline_desc = pip.m_desc;
			if (pixelshader) pipeline_desc.PS = pixelshader->m_shader_bytecode;
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
			.texture2D(1024, 1024)
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
	return result;
}

void renderman::render_shadows(renderscene& scene, const contentman& cman)
{
	D3D12_RESOURCE_DESC shadowmap_dxdesc{};
	if (!m_shadows.m_resource.get_dxdesc(shadowmap_dxdesc))
	{
		return;
	}

	cmd_transition_barrier(m_shadows.m_resource, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	D3D12_CPU_DESCRIPTOR_HANDLE shadowmap_dsv_handle;
	get_descheap(descriptor_heap::dsv).get_handles(m_shadows.m_dsv.m_heap_idx, &shadowmap_dsv_handle);
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
	m_cmdlist->SetPipelineState(m_pipelines[pip_shadows].m_dxpipeline);
	m_cmdlist->SetGraphicsRootSignature(m_pipelines[pip_shadows].m_dxsignature);
	m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::view, view::light).m_resource->GetGPUVirtualAddress());
	m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer.m_resource->GetGPUVirtualAddress());

	for (const auto& pair : scene.m_batch_instance_lookup)
	{
		const renderscene::batch_key& batch_key = pair.first;
		const mesh_id mesh = batch_key.m_mesh;
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

	cmd_transition_barrier(m_shadows.m_resource, D3D12_RESOURCE_STATE_DEPTH_READ);
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
	const uint2 backbuffer_size = swapchain.m_current_size;

	m_cmd_allocator->Reset();
	m_cmdlist->Reset(m_cmd_allocator, nullptr);

	// clear & re-bind bindless resource heaps
	clear_gpu_heaps();
	const descheap& rtv_heap = get_descheap(descriptor_heap::rtv);
	const descheap& dsv_heap = get_descheap(descriptor_heap::dsv);

	dxdescheap* global_descheaps[]{
		get_descheap(descriptor_heap::gpu_resource).m_dxheap,
		get_descheap(descriptor_heap::gpu_sampler).m_dxheap
	};
	m_cmdlist->SetDescriptorHeaps(_countof(global_descheaps), global_descheaps);
	m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// upload pending textures & meshbuffers
	upload_buffers();
	
	// map instance buffer -> GPU
	const bool any_meshes = !scene.m_mesh_instances.empty() && !scene.m_batch_instance_lookup.empty();
	if (any_meshes)
	{
		map_resource<gpu_instance>(m_instancebuffer, [this, &scene](gpu_instance* instance) {
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
	}
	
	// map constant buffers -> GPU
	{
		// cbuffer: light view (shadows)
		m_cbuffers.write_data(cbuffer::view, view::light, [&scene](void* dest) {
			gpu_cbuffer_view* dest_view = reinterpret_cast<gpu_cbuffer_view*>(dest);
			const auto light_transform = transform::build({ 0,0,0 }, make_look_rotation(scene.m_light.m_direction), { 1,1,1 });
			const auto mat_view = calculate_view_mat(light_transform.m_matrix);
			const float orthographic_size = 10.0f;
			const auto mat_proj = calculate_orthographic_proj_mat(
				{ -orthographic_size, orthographic_size },
				{ -orthographic_size, orthographic_size },
				{ -orthographic_size, orthographic_size });
			dest_view->m_viewprojection = mat_proj * mat_view;
			scene.m_light.m_mat_to_lightspace = dest_view->m_viewprojection;
		});

		// cbuffer: global
		m_cbuffers.write_data(cbuffer::global, 0u, [this, &scene](void* dest)
		{
			gpu_cbuffer_global* global = reinterpret_cast<gpu_cbuffer_global*>(dest);
			global->m_light_color = scene.m_light.m_color;
			global->m_light_direction = scene.m_light.m_direction;
			global->m_mat_to_lightspace = scene.m_light.m_mat_to_lightspace;
		
			push_gpu_resource_descriptor(*m_device, m_shadows.m_srv, global->m_texid_shadows);
		});

		// cbuffer: main view
		m_cbuffers.write_data(cbuffer::view, view::main, [&scene, &backbuffer_size](void* dest) {
			gpu_cbuffer_view* dest_view = reinterpret_cast<gpu_cbuffer_view*>(dest);
			const auto mat_view = calculate_view_mat(scene.m_camera.m_transform.m_matrix);
			const auto mat_proj = calculate_perspective_proj_mat(scene.m_camera.m_fov,
				(float)backbuffer_size.x / (float)backbuffer_size.y,
				scene.m_camera.m_near,
				scene.m_camera.m_far);
			dest_view->m_viewprojection = mat_proj * mat_view;
		});
	}

	// [shadow rendering]
	if (any_meshes)
	{
		render_shadows(scene, cman);
	}

	// [backbuffer rendering]
	{
		gpu_resource& current_backbuffer = swapchain.get_current_backbuffer_resource();
		const descriptor& current_backbuffer_rtv = swapchain.get_current_backbuffer_rtv();
		
		D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv_handle, backbuffer_dsv_handle;
		rtv_heap.get_handles(current_backbuffer_rtv.m_heap_idx, &backbuffer_rtv_handle).claim();
		dsv_heap.get_handles(swapchain.m_dsv.m_heap_idx, &backbuffer_dsv_handle).claim();

		cmd_transition_barrier(current_backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);

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

		// shading passes: pip_shading | pip_wireframe
		if (any_meshes)
		{
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
				m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::global).m_resource->GetGPUVirtualAddress());
				m_cmdlist->SetGraphicsRootConstantBufferView(1, m_cbuffers.resource(cbuffer::view, view::main).m_resource->GetGPUVirtualAddress());
				m_cmdlist->SetGraphicsRootShaderResourceView(2, m_instancebuffer.m_resource->GetGPUVirtualAddress());
				
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
			map_resource<gpu_line_instance>(m_instancebuffer_lines, [this, &scene, num_instances](gpu_line_instance* instance) {
				memcpy(instance, scene.m_line_instances.data(), sizeof(gpu_line_instance) * num_instances);
			});

			m_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
			m_cmdlist->SetGraphicsRootConstantBufferView(0, m_cbuffers.resource(cbuffer::global).m_resource->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootConstantBufferView(1, m_cbuffers.resource(cbuffer::view, view::main).m_resource->GetGPUVirtualAddress());
			m_cmdlist->SetGraphicsRootShaderResourceView(2, m_instancebuffer_lines.m_resource->GetGPUVirtualAddress());
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
			map_resource<gpu_ui_instance>(m_instancebuffer_ui, [this, &scene, num_instances](gpu_ui_instance* instance) {
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
			m_cmdlist->SetGraphicsRootShaderResourceView(1, m_instancebuffer_ui.m_resource->GetGPUVirtualAddress());
			m_cmdlist->DrawInstanced(6, num_instances, 0, 0);
		}

		cmd_transition_barrier(current_backbuffer, D3D12_RESOURCE_STATE_PRESENT);
	}
	
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

			reallocate_image_texture(cman, instance.m_img_basecolor);
			reallocate_skeleton_buffers(cman, instance.m_skeleton);

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
		if (!m_instancebuffer_lines.is_valid() || m_instancebuffer_lines.m_resource->GetDesc().Width < bytesize)
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
}

void renderman::upload_buffers()
{
	for (auto& pair : m_mesh_buffers)
	{
		meshbuffers& buffers = pair.second;
		if (!buffers.m_uploaded)
		{
			m_cmdlist->CopyResource(buffers.m_vertbuffer.m_resource, buffers.m_vertex_stagingbuffer.m_resource);
			m_cmdlist->CopyResource(buffers.m_indbuffer.m_resource, buffers.m_index_stagingbuffer.m_resource);
			cmd_transition_barrier(buffers.m_vertbuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			cmd_transition_barrier(buffers.m_indbuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			buffers.m_uploaded = true;
		}
	}
	for (auto& pair : m_skel_buffers)
	{
		skeletonbuffers& buffers = pair.second;
		if (!buffers.m_uploaded)
		{
			m_cmdlist->CopyResource(buffers.m_bone_buffer.m_resource, buffers.m_bone_buffer_staging.m_resource);
			cmd_transition_barrier(buffers.m_bone_buffer, D3D12_RESOURCE_STATE_GENERIC_READ);
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
			cmd_transition_barrier(tex.m_gpu_resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			tex.m_uploaded = true;
		}

		// push each texture onto the gpu heap
		push_gpu_resource_descriptor(*m_device, tex.m_srv, tex.m_gpu_heap_slot);
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
	if (!buffers.m_bone_buffer.is_valid())
	{
		release_if_valid(buffers.m_bone_buffer);
		release_if_valid(buffers.m_skinned_buffer);

		buffers.m_bone_buffer_staging = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.init_state(D3D12_RESOURCE_STATE_COPY_SOURCE)
			.heap_type(D3D12_HEAP_TYPE_UPLOAD)).claim();

		// map the upload resource
		map_resource<gpu_bone>(buffers.m_bone_buffer_staging, [num_bones, &skeleton_asset](gpu_bone* dest)
		{
			for (uint32 i = 0u; i < num_bones; ++i)
			{
				dest[i].m_matrix = skeleton_asset->m_bones[i].m_offset_matrix;
				dest[i].m_parent = skeleton_asset->m_bones[i].m_parent_idx;
			}
		}).claim();

		buffers.m_bone_buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.buffer_single(bytesize)
			.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
		).claim();

		buffers.m_skinned_buffer = gpu_resource::allocate(*m_device, gpu_resource::builder()
			.buffer_single(bytesize)
			.init_state(D3D12_RESOURCE_STATE_COPY_DEST)
		).claim();
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

void renderman::cmd_transition_barrier(gpu_resource& resource, D3D12_RESOURCE_STATES after)
{
	cmd_transition_barrier(*resource.m_resource, resource.m_current_state, after);
	resource.m_previous_state = resource.m_current_state;
	resource.m_current_state = after;
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