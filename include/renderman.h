#pragma once

#include "common.h"
#include "contentman.h"
#include "logman.h"

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
	float2 m_uv;
	uint4 m_bone_ids;
	float4 m_bone_weights;
};
struct gpu_instance
{
	mat4x4 m_transform;
	float4 m_color;
	uint32 m_tex_basecolor_idx;
};
struct gpu_ui_instance
{
	float4 m_box;
	uint32 m_tex_heap_idx;
};
struct gpu_bone
{
	float4x4 m_matrix;
	int m_parent;
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
using dxpipeline = ID3D12PipelineState;

enum class shader
{
	shaded,
	wireframe,
	num
};

class renderscene final
{
public:
	struct {
		transform m_transform;
		float m_fov;
		float m_near;
		float m_far;
	} m_camera;
	struct {
		float4 m_color;
		float4 m_direction;
	} m_light;
	
	struct batch_key
	{
		mesh_id m_mesh;
		shader m_shader;

		struct hash_t {
			uint64 operator()(const batch_key& k) const {
				return ((uint64)k.m_mesh ^ (uint64)k.m_shader) << 1;
			}
		};
		struct equal_t {
			static bool operator()(const batch_key& lhs, const batch_key& rhs)
			{
				return lhs.m_mesh == rhs.m_mesh && lhs.m_shader == rhs.m_shader;
			}
		};
	};

	struct mesh_instance
	{
		transform m_transform;
		float4 m_color;
		float m_time;

		// bindings
		mesh_id m_mesh				= k_id_invalid;
		skel_id m_skeleton			= k_id_invalid;
		shader m_shader				= shader::shaded;
		image_id m_img_basecolor	= k_id_invalid;

		void apply_material(const contentman& cman, const mat_id mat);
	};

	struct ui_instance
	{
		float4 m_box;
		image_id m_image;
	};

	mesh_instance& add_mesh_instance(const mesh_id mesh, const shader shdr)
	{
		m_batch_instance_lookup[{mesh, shdr}].push_back((uint32)m_mesh_instances.size());
		m_mesh_instances.push_back({});
		m_mesh_instances.back().m_mesh = mesh;
		return m_mesh_instances.back();
	}

	ui_instance& add_ui_instance()
	{
		m_ui_instances.push_back({});
		return m_ui_instances.back();
	}

	uint32 get_meshbatch_num_instances(const batch_key& key) const
	{
		return (uint32)m_batch_instance_lookup.at(key).size();
	}

	uint32 get_meshbatch_first_instance(const batch_key& key) const
	{
		uint32 offset = 0;
		for (const auto& pair : m_batch_instance_lookup)
		{
			if (batch_key::equal_t::operator()(key, pair.first)) return offset;
			offset += (uint32)pair.second.size();
		}
		return 0;
	}

	umap<batch_key, vector<uint32>, batch_key::hash_t, batch_key::equal_t> m_batch_instance_lookup;
	vector<mesh_instance> m_mesh_instances;
	vector<ui_instance> m_ui_instances;
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
			dxresource* m_depth;
			descriptor m_rtvs[k_num_swapchain_buffers];
			descriptor m_dsv;
			dxresource* m_uav_proxy_resource;
			descriptor m_uav_proxy;
			uint2 m_current_size;

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

	enum pipeline
	{
		cpip_skinning,
		cpip_num,

		pip_shading,
		pip_wireframe,
		pip_ui,
		pip_num = pip_ui - cpip_num + 2
	};

	static constexpr bool is_pipeline_compute(pipeline pip)
	{
		return pip < cpip_num;
	}
	static constexpr bool is_pipeline_graphics(pipeline pip)
	{
		return pip > cpip_num && pip < pip_num;
	}

	struct pipeline_desc final
	{
		string m_shaders_filepath;
		string m_vs_entrypoint;
		string m_vs_target;
		string m_ps_entrypoint;
		string m_ps_target;
		string m_cs_entrypoint;
		string m_cs_target;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC m_desc;
		D3D12_COMPUTE_PIPELINE_STATE_DESC m_compute_desc;
		vector<D3D12_INPUT_ELEMENT_DESC> m_input_elements;
		dxpipeline* m_dxpipeline;
		dxsignature* m_dxsignature;
	};
	vector<pipeline_desc> m_pipelines;

	struct meshbuffers final
	{
		dxresource* m_vertex_stagingbuffer;
		dxresource* m_index_stagingbuffer;
		dxresource* m_vertbuffer;
		dxresource* m_indbuffer;
		D3D12_VERTEX_BUFFER_VIEW m_vtb_view;
		D3D12_INDEX_BUFFER_VIEW m_idx_view;
		bool m_uploaded = false;

		uint32 get_num_vertices() const { return m_vertbuffer ? (uint32)(m_vertbuffer->GetDesc().Width / sizeof(gpu_vertex)) : 0; }
		uint32 get_num_indices() const { return m_indbuffer ? (uint32)(m_indbuffer->GetDesc().Width / sizeof(uint32)) : 0; }
	};
	umap<mesh_id, meshbuffers> m_mesh_buffers;
	dxresource* m_instancebuffer;
	dxresource* m_instancebuffer_ui;
	descriptor m_instancebuffer_srv;
	descriptor m_instancebuffer_ui_srv;
	dxresource* m_constantbuffer;
	descriptor m_constantbuffer_cbv;
	dxresource* m_bonebuffer;
	descriptor m_bonebuffer_srv;

	struct skeletonbuffers final
	{
		dxresource* m_bone_buffer;			// static bone data from content
		dxresource* m_bone_buffer_staging;	// static bone data from content
		dxresource* m_skinned_buffer;		// output of the GPU skinning
		bool m_uploaded = false;
	};
	umap<skel_id, skeletonbuffers> m_skel_buffers;

	struct texture
	{
		dxresource* m_staging_resource;
		dxresource* m_gpu_resource;
		descriptor m_srv;

		bool m_uploaded = false;
		uint32 m_gpu_heap_slot = 0;
	};
	umap<image_id, texture> m_image_textures;

public:
	result<> initialize();

	void compile_shaders();

	void compile_pipelines();

	void cmd_transition_barrier(dxresource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	void clear_gpu_heaps();

	bool push_gpu_resource_descriptor(
		dxdevice& device, 
		const descriptor& source_descriptor,
		uint32& out_gpu_heap_index,
		D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu_handle);

	void render(renderscene& scene, const contentman& contentman);

	struct descriptor_args final
		{
			D3D12_BUFFER_SRV m_buffer_srv{};
		};
	result<descriptor> create_resource_descriptor(dxresource& resource, descriptor_type type, bool gpu_readable, const descriptor_args& args = {});

	result<> register_window(void* platform_handle);

private:
	void process_scene(renderscene& scene, const contentman& cman);
	void upload_buffers();
	void reallocate_image_texture(const contentman& cman, image_id id);
	void reallocate_skeleton_buffers(const contentman& cman, skel_id id);
	static void populate_vertex_shader_input(renderman::pipeline_desc& pipeline);
};
}