#pragma once

#include "common.h"
#include "contentman.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <atlbase.h>
#include <conio.h>

#include "dxcapi.h"

namespace strikers {
struct gpu_vertex
{
	float3 m_position;
	float3 m_normal;
};
struct gpu_instance
{
	mat4x4 m_transform;
	float4 m_color;
};
struct gpu_cbuffer
{
	mat4x4 m_viewprojection;
	float4 m_light_color;
	float4 m_light_direction;
};

class contentman;
using dxdevice = ID3D12Device;
using dxswapchain = IDXGISwapChain4;
using dxadapter = IDXGIAdapter1;
using dxqueue = ID3D12CommandQueue;
using dxfactory = IDXGIFactory7;
using dxallocator = ID3D12CommandAllocator;
using dxcmdlist = ID3D12GraphicsCommandList;
using dxdescheap = ID3D12DescriptorHeap;
using dxresource = ID3D12Resource;
using dxfence = ID3D12Fence;
using dxstate = ID3D12StateObject;
using dxsignature = ID3D12RootSignature;
using dxresource_state = D3D12_RESOURCE_STATES;
using dxpipeline_graphics = ID3D12PipelineState;

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

	D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_COMMON;
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
	bool allow_uav,
	D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_COMMON)
{
	using restype = result<dxresource*>;

	D3D12_HEAP_PROPERTIES heap_props{};
	heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC desc = {};
	switch (num_dimensions)
	{
	case 1: desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D; break;
	case 2: desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; break;
	case 3: desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D; break;
	default:
		return restype::make_fail("texture of dimensions !1, !2 or !3 are not supported");
	}
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Width = dimensions.x;
	desc.Height = dimensions.y;
	desc.DepthOrArraySize = dimensions.z;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	// desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (allow_uav) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

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

	class renderscene final
	{
	public:
		struct
		{
			transform m_transform;
			float m_fov;
			float m_near;
			float m_far;
		} m_camera;

		struct
		{
			float4 m_color;
			float4 m_direction;
		} m_light;
		
		struct instance
		{
			transform m_transform;
			float4 m_color;
			mesh_id m_mesh;
			bool m_ignored;
		};

		instance& add_instance(const mesh_id mesh)
		{
			m_batch_instance_lookup[mesh].push_back((uint32)m_instances.size());
			m_instances.push_back({});
			m_instances.back().m_mesh = mesh;
			return m_instances.back();
		}

		uint32 get_batch_num_instances(const mesh_id mesh) const
		{
			return (uint32)m_batch_instance_lookup.at(mesh).size();
		}

		uint32 get_batch_first_instance(const mesh_id mesh) const
		{
			uint32 offset = 0;
			for (const auto& pair : m_batch_instance_lookup)
			{
				if (pair.first == mesh) return offset;
				offset += (uint32)pair.second.size();
			}
			return 0;
		}

		using batch_key = mesh_id;
		umap<batch_key, vector<uint32>> m_batch_instance_lookup;
		vector<instance> m_instances;
	};

	enum class shader_type
	{
		vs,
		ps,
		lib,
		num
	};

	class shaderman final
	{
	public:
		using shader_key = uint64;
		static shader_key make_shader_key(
			const stringview& filepath, 
			const stringview& entrypoint,
			const stringview& target)
		{
			string copy1 = normalize_path(filepath);
			string copy2 = normalize_path(entrypoint);
			string copy3 = normalize_path(target);
			const uint64 hash1 = std::hash<stringview>{}(copy1);
			const uint64 hash2 = std::hash<stringview>{}(copy2);
			const uint64 hash3 = std::hash<stringview>{}(copy3);

			// hash combine (boost-style)
			return static_cast<shader_key>(
				hash3 ^ hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2))
				);
		}
		struct shader_entry final
		{
			ID3DBlob* m_shaderlib;
			IDxcBlob* m_blob_rootsignature;
			dxstate* m_state_object;
			dxsignature* m_signature;

			D3D12_SHADER_BYTECODE m_shader_bytecode;

			ID3D12StateObjectProperties1* m_state_object_props;
			ID3D12WorkGraphProperties* m_workgraph_props;
			D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS m_graph_mem_requirements;
			D3D12_PROGRAM_IDENTIFIER m_program_id;
			dxresource* m_backing_memory;
			uint32 m_workgraph_idx;
			uint32 m_entrypoint_idx;
		};

	private:
		HMODULE dll_dxcompiler;
		IDxcUtils* m_utils;
		IDxcCompiler3* m_compiler;
		umap<shader_key, shader_entry> m_shaders;
		result<IDxcOperationResult*> compile_dxc_file(const DxcBuffer& filebuffer, const vector<string>& args)
		{
			using restype = result<IDxcOperationResult*>;
			
			vector<wstring> wargs{};
			for (const string& str : args)
			{
				wargs.push_back(to_wstring(str));
			}
			vector<LPCWSTR> converted_args{};
			for (const wstring& wstr : wargs)
			{
				converted_args.push_back(wstr.c_str());
			}

			IDxcOperationResult* operation_result;
			HRESULT hres = m_compiler->Compile(&filebuffer, converted_args.data(), (uint32)converted_args.size(), nullptr, IID_PPV_ARGS(&operation_result));
			if (!SUCCEEDED(hres))
			{
				return restype::make_fail("");
			}

			return restype::make_success(operation_result);
		}

	public:
		shaderman() { initialize(); }

		result<> initialize()
		{
			using restype = result<>;
			dll_dxcompiler = LoadLibrary(L"dxcompiler.dll");

			DxcCreateInstanceProc dxc_create_instance_func;
			dxc_create_instance_func = (DxcCreateInstanceProc)GetProcAddress(dll_dxcompiler, "DxcCreateInstance");
			if (dxc_create_instance_func == nullptr)
				return restype::make_fail("");

			if (!SUCCEEDED(dxc_create_instance_func(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils))))
				return restype::make_fail("");

			if (!SUCCEEDED(dxc_create_instance_func(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler))))
				return restype::make_fail("");

			return restype::make_success();
		}

		result<> compile_shader(
			const stringview& filepath, 
			const stringview& entrypoint, 
			const stringview& target,
			dxdevice& device);

		result<shader_entry const*> get_shader(
			const stringview& filepath,
			const stringview& entrypoint,
			const stringview& target) const
		{
			using restype = result<shader_entry const*>;
			const auto& key = make_shader_key(filepath, entrypoint, target);
			if (!m_shaders.contains(key))
			{
				return restype::make_fail("");
			}
			else return &m_shaders.at(key);
		}
	};

	class renderman final
	{
		static constexpr uint32 k_num_swapchain_buffers = 3;

#pragma region descriptors
		enum class descriptor_heap
		{
			rtv,
			dsv,
			resource,
			sampler,
			gpu_resource,
			gpu_sampler,
			num
		};
		static constexpr uint32 k_num_descriptor_heaps = (uint32)descriptor_heap::num;
		static constexpr uint32 k_descriptor_heap_nums[k_num_descriptor_heaps]
		{
			16,
			16,
			16,
			16,
			16,
			16
		};
		static const uint32 get_descheap_size(descriptor_heap heap)
		{
			static const uint32 k_sizes[k_num_descriptor_heaps]
			{
				16,
				16,
				16,
				16,
				16,
				16
			};
			return k_sizes[(int)heap];
		}
		static const D3D12_DESCRIPTOR_HEAP_TYPE get_descheap_type(descriptor_heap heap)
		{
			static const D3D12_DESCRIPTOR_HEAP_TYPE k_types[k_num_descriptor_heaps]
			{
				D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
				D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
			};
			return k_types[(int)heap];
		}
		static const bool is_descheap_gpu_readable(descriptor_heap heap)
		{
			return heap == descriptor_heap::gpu_resource || heap == descriptor_heap::gpu_sampler;
		}
		enum class descriptor_type
		{
			rtv,
			dsv,
			uav,
			cbv,
			srv,
			sampler,
			num
		};
		static constexpr uint32 k_num_descriptor_types = (uint32)descriptor_type::num;
		static const bool can_gpu_read_descriptor(descriptor_type type)
		{
			return (type != descriptor_type::rtv) && (type != descriptor_type::dsv);
		}
		static const descriptor_heap get_descheap_type(descriptor_type type, bool gpu_readable)
		{
			descriptor_heap heap_type{};
			switch (type)
			{
			case descriptor_type::rtv:
				return descriptor_heap::rtv;
				break;
			case descriptor_type::dsv:
				return descriptor_heap::dsv;
				break;
			case descriptor_type::uav:
			case descriptor_type::cbv:
			case descriptor_type::srv:
				return (gpu_readable ? descriptor_heap::gpu_resource : descriptor_heap::resource);
			case descriptor_type::sampler:
				return (gpu_readable ? descriptor_heap::gpu_sampler : descriptor_heap::sampler);
				break;
			}
			return descriptor_heap::num;
		}
		struct descriptor final
		{
			descriptor_heap m_owner;
			descriptor_type m_type;
			uint32 m_heap_idx;
		};
#pragma endregion

		dxfactory* m_factory;
		dxdevice* m_device;
		dxqueue* m_queue;
		dxadapter* m_adapter;
		dxallocator* m_cmd_allocator;
		dxcmdlist* m_cmdlist;
		dxfence* m_frame_fence;
		uint64 m_frame;
		shaderman m_shaderman;

		struct resource
		{
			dxresource* m_dxresource;
			dxresource_state m_previous_state;
		};

		struct swapchain final
		{
			dxswapchain* m_swapchain;
			dxresource* m_buffers[k_num_swapchain_buffers];
			descriptor m_rtvs[k_num_swapchain_buffers];
			dxresource* m_uav_proxy_resource;
			descriptor m_uav_proxy;

			uint32 get_current_backbuffer_idx() const
			{
				return m_swapchain->GetCurrentBackBufferIndex();
			}
			const descriptor& get_current_backbuffer_rtv() const
			{
				return m_rtvs[get_current_backbuffer_idx() % k_num_swapchain_buffers];
			}
			dxresource& get_current_backbuffer_resource() const
			{
				return *m_buffers[get_current_backbuffer_idx() % k_num_swapchain_buffers];
			}
		};
		vector<swapchain> m_swapchains{};
		umap<void*, uint32> m_swapchain_lookup{};

		struct descheap final
		{
			dxdescheap* m_dxheap;
			uint32 m_handle_size;
			uint32 m_stack_ptr;
			descriptor_heap m_type;

			result<uint32> allocate()
			{
				using restype = result<uint32>;
				if (m_stack_ptr >= k_descriptor_heap_nums[(uint32)m_type])
				{
					return restype::make_fail("out of descriptors!");
				}

				return m_stack_ptr++;
			}

			bool is_descriptor_id_valid(uint32 id) const
			{
				return id < m_stack_ptr;
			}

			result<> get_handles(uint32 id, 
				D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
				D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle = nullptr) const
			{
				using restype = result<>;
				if (!is_descriptor_id_valid(id)) 
					return restype::make_fail("get_handles() > invalid id!");
				
				if (out_cpu_handle)
				{
					*out_cpu_handle = (D3D12_CPU_DESCRIPTOR_HANDLE)(m_dxheap->GetCPUDescriptorHandleForHeapStart().ptr + (id * m_handle_size));
				}
				if (out_gpu_handle && is_descheap_gpu_readable(m_type))
				{
					*out_gpu_handle = (D3D12_GPU_DESCRIPTOR_HANDLE)(m_dxheap->GetGPUDescriptorHandleForHeapStart().ptr + (id * m_handle_size));
				}
				return {};
			}
		};
		descheap m_descheaps[k_num_descriptor_heaps];
		descheap& get_descheap(descriptor_heap type)
		{
			m_descheaps[(int)type].m_type = type;
			return m_descheaps[(int)type];
		}

		struct graphics_pipeline
		{
			string m_shaders_filepath;
			string m_vs_entrypoint;
			string m_vs_target;
			string m_ps_entrypoint;
			string m_ps_target;
			D3D12_GRAPHICS_PIPELINE_STATE_DESC m_desc;
			vector<D3D12_INPUT_ELEMENT_DESC> m_input_elements;
			dxpipeline_graphics* m_dxpipeline;
			dxsignature* m_dxsignature;
		};
		vector<graphics_pipeline> m_pipelines;

		struct meshbuffers final
		{
			dxresource* m_vertbuffer;
			dxresource* m_indbuffer;
			D3D12_VERTEX_BUFFER_VIEW m_vtb_view;
			D3D12_INDEX_BUFFER_VIEW m_idx_view;

			uint32 get_num_vertices() const { return m_vertbuffer ? m_vertbuffer->GetDesc().Width / sizeof(gpu_vertex) : 0; }
			uint32 get_num_indices() const { return m_indbuffer ? m_indbuffer->GetDesc().Width / sizeof(uint32) : 0; }
		};
		umap<mesh_id, meshbuffers> m_mesh_buffers;
		dxresource* m_instancebuffer;
		descriptor m_instancebuffer_srv;
		dxresource* m_constantbuffer;
		descriptor m_constantbuffer_cbv;

	public:
		template <typename _restype>
		_restype make_result(HRESULT res)
		{
			if (!FAILED(res)) return _restype::make_success();
			else
			{
				const string error_str = hr_to_string(res);
				return _restype::make_fail(error_str.c_str());
			}
		}

		result<> initialize();

		void compile_shaders();

		void compile_pipelines();

		void cmd_transition_barrier(dxresource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
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

		void clear_gpu_heaps()
		{
			get_descheap(descriptor_heap::gpu_resource).m_stack_ptr = 0;
			get_descheap(descriptor_heap::gpu_sampler).m_stack_ptr = 0;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE push_gpu_resource_descriptor(dxdevice& device, const descriptor& source_descriptor)
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
			return dest_gpu_handle;
		}

		void render(renderscene& scene, const contentman& contentman);

		struct descriptor_args final
		{
			D3D12_BUFFER_SRV m_buffer_srv{};
		};

		result<descriptor> create_resource_descriptor(
			dxresource& resource,
			descriptor_type type,
			bool gpu_readable,
			const descriptor_args& args = {})
		{
			using restype = result<descriptor>;
			restype result;

			if (gpu_readable && !can_gpu_read_descriptor(type))
				return restype::make_fail("create_resource_descriptor(type, gpu_readable) failed > descriptor of type cannot be gpu_readable!");

			const descriptor_heap heap_type = get_descheap_type(type, gpu_readable);

			// allocate an entry on the target heap, and return the descriptor
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
				view_desc.SizeInBytes = resource.GetDesc().Width;
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

		result<> register_window(void* platform_handle)
		{
			using restype = result<>;
			restype result;

			if (m_swapchain_lookup.contains(platform_handle))
			{
				return restype::make_warning("skipping register: window at handle already registered!");
			}

			const uint2 dimensions = { 640, 480 };

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
				result = make_result<restype>(m_factory->CreateSwapChainForHwnd(
					m_queue,
					hwnd,
					&scDesc,
					nullptr,
					nullptr,
					&temp_swapchain));

				m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
				
				swapchain& target_swapchain = m_swapchains.back();
				target_swapchain.m_swapchain = (dxswapchain*)temp_swapchain;

				// gather the resources, create the RTVS
				for (uint32 i = 0u; i < k_num_swapchain_buffers; ++i)
				{
					dxresource*& buffer = target_swapchain.m_buffers[i];
					target_swapchain.m_swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer));
					target_swapchain.m_rtvs[i] = create_resource_descriptor(*buffer, descriptor_type::rtv, false).claim();
				}

				// make uav proxy
				target_swapchain.m_uav_proxy_resource = allocate_gpu_texture(*m_device, 2, { dimensions.x, dimensions.y, 1 }, true, D3D12_RESOURCE_STATE_COPY_SOURCE).claim();
				target_swapchain.m_uav_proxy = create_resource_descriptor(*target_swapchain.m_uav_proxy_resource, descriptor_type::uav, false).claim();
			}
			
			return {};
		}
	};
}