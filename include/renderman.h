#pragma once

#include "common.h"
#include "contentman.h"
#include "logman.h"

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
struct gpu_cbuffer_global
{
	float4x4 m_mat_to_lightspace;
	float4 m_light_color;
	float4 m_light_direction;
	uint32 m_texid_shadows;
};
struct gpu_cbuffer_view
{
	mat4x4 m_viewprojection;
};
struct gpu_cbuffer_material
{

};
struct gpu_line_instance
{
	float4 m_color;
	float3 m_point_a;
	float3 m_point_b;
};

struct cbuffer final
{
	enum slot
	{
		global,
		view,
		num
	};

	static uint64 get_bytesize(slot slt)
	{
		switch (slt)
		{
		case cbuffer::slot::global: return sizeof(gpu_cbuffer_global);
		case cbuffer::slot::view: return sizeof(gpu_cbuffer_view);
		}
		return 0u;
	}
};

struct view final
{
	enum slot
	{
		main,
		light,
		num
	};
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
		float4x4 m_mat_to_lightspace;
		transform m_transform;
		float4 m_color;
		box m_frustrum;
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
		
		mesh_instance& transform(const transform& trans)
		{
			m_transform = trans;
			return *this;
		}
	};

	struct ui_instance
	{
		float4 m_box;
		image_id m_image;
	};

	struct line_instance
	{
		float4 m_color;
		float3 m_point_a;
		float3 m_point_b;
	};

	struct line_builder
	{
		renderscene& m_owner;
		line_builder(renderscene& owner) : m_owner{ owner } {}

		const line_builder& add_line(const float3& point_a, const float3& point_b, const float4& color) const
		{
			line_instance& line = m_owner.add_line_instance();
			line.m_point_a = point_a;
			line.m_point_b = point_b;
			line.m_color = color;
			return *this;
		}
		const line_builder& add_sphere(const transform& transform, const sphere& sphere, const float4& color) const;
		const line_builder& add_box(const transform& transform, const box& box, const float4& color) const;
		const line_builder& add_transform(const transform& transform) const;
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

	line_instance& add_line_instance()
	{
		m_line_instances.push_back({});
		return m_line_instances.back();
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
	vector<line_instance> m_line_instances;
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

struct descriptor_heap final
{
	enum slot
	{
		rtv,
		dsv,
		resource,
		sampler,
		gpu_resource,
		gpu_sampler,
		num
	};

	static const uint32 capacity(slot slt)
	{
		static const uint32 c_capacities[num]{
			16,
			16,
			32, // resource
			16,
			32, // gpu_resource
			16
		};
		return c_capacities[slt];	
	}

	static const D3D12_DESCRIPTOR_HEAP_TYPE dxtype(slot slt)
	{
		static const D3D12_DESCRIPTOR_HEAP_TYPE k_types[num]
		{
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
		};
		return k_types[(int)slt];
	}

	static const bool is_gpu_heap(slot slt)
	{
		return slt == gpu_resource || slt == gpu_sampler;
	}
};

struct descriptor final
{
	enum type
	{
		rtv,
		dsv,
		uav,
		cbv,
		srv,
		sampler,
		num
	};

	struct builder final
	{
		type m_type;
		uint32 m_first_element;
		uint32 m_num_elements;
		uint32 m_bytestride;
		DXGI_FORMAT m_format;
		bool m_view_as_array = false;
		bool m_gpu_readable = false;
		bool m_format_override = false;

		builder& type(type type) { m_type = type; return *this; }
		builder& gpu_heap(bool yes) { m_gpu_readable = yes; return *this; }
		builder& view_as_array(bool yes) { m_view_as_array = yes; return *this; }
		builder& format(DXGI_FORMAT format) { m_format = format; m_format_override = true; return *this; }

		// only relevant for buffers:
		builder& bff_first_element(uint32 first) { m_first_element = first; return *this; }
		builder& bff_num_elements(uint32 num) { m_num_elements = num; return *this; }
		builder& bff_bytestride(uint32 stride) { m_bytestride = stride; return *this; }
	};

	static const bool is_gpu_readable(type tpe)
	{
		return tpe != rtv && tpe != dsv;
	}

	static const descriptor_heap::slot dest_heap(type tpe, bool gpu_readable)
	{
		switch (tpe)
		{
		case descriptor::rtv:
			return descriptor_heap::rtv;
			break;
		case descriptor::dsv:
			return descriptor_heap::dsv;
			break;
		case descriptor::uav:
		case descriptor::cbv:
		case descriptor::srv:
			return (gpu_readable ? descriptor_heap::gpu_resource : descriptor_heap::resource);
		case descriptor::sampler:
			return (gpu_readable ? descriptor_heap::gpu_sampler : descriptor_heap::sampler);
			break;
		}
		return descriptor_heap::num;
	}

	descriptor_heap::slot m_owner;
	type m_type;
	uint32 m_heap_idx;
};

struct gpu_resource
{
	enum type
	{
		buffer,
		texture,
		num
	};

	struct builder
	{
		uint64 m_bytestride;
		uint64 m_bytesize;
		uint32 m_num_mips = 1;
		uint3 m_sizes = uint3(1, 1, 1);
		type m_type = type::buffer;
		D3D12_RESOURCE_STATES m_init_state = D3D12_RESOURCE_STATE_COMMON;
		D3D12_HEAP_TYPE m_heap_type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_FLAGS m_create_flags = D3D12_RESOURCE_FLAG_NONE;
		DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
		float m_clear_value_depth = 0.0f;
		uint8 m_clear_value_stencil = 0u;
		float4 m_clear_value_color = {};
		bool m_has_clear_value = false;

		builder& size_x(uint32 size) { m_sizes.x = size; return *this; }
		builder& size_y(uint32 size) { m_sizes.y = size; return *this; }
		builder& size_z(uint32 size) { m_sizes.z = size; return *this; }
		builder& bytestride(uint64 stride) { m_bytestride = stride; return *this; }
		builder& bytesize(uint64 size) { m_bytesize = size; return *this; }
		builder& init_state(D3D12_RESOURCE_STATES state) { m_init_state = state; return *this; }
		builder& heap_type(D3D12_HEAP_TYPE type) { m_heap_type = type; return *this; }
		builder& type(type tpe) { m_type = tpe; return *this; }
		builder& num_mips(uint32 num_mips) { m_num_mips = num_mips; return *this; }
		builder& create_flags(D3D12_RESOURCE_FLAGS flags) { m_create_flags = flags; return *this; }
		builder& format(DXGI_FORMAT format) { m_format = format; return *this; }

		builder& clear_value_color(const float4& color) { m_clear_value_color = color; m_has_clear_value = true; return *this; }
		builder& clear_value_depth(const float depth) { m_clear_value_depth = depth; m_has_clear_value = true;  return *this; }
		builder& clear_value_stencil(const uint8 stencil) { m_clear_value_stencil = stencil; m_has_clear_value = true; return *this; }

		builder& texture2D(uint32 x, uint32 y) {
			return type(type::texture).size_x(x).size_y(y);
		}

		template <typename _t>
		builder& buffer_with_num(uint32 num) {
			return bytestride(sizeof(_t))
				.bytesize(sizeof(_t) * num)
				.type(gpu_resource::buffer);
		}

		builder& buffer_single(uint64 bytesize)
		{
			return bytestride(bytesize)
				.bytesize(bytesize)
				.type(type::buffer);
		}

		builder& cbuffer()
		{
			// D3D12 ERROR: ID3D12Device::CreateConstantBufferView: Size of 32 is invalid.  
			// Device requires SizeInBytes be a multiple of 256
			return bytesize(max(256u, (uint32)m_bytesize));
		}
	};

	static result<gpu_resource> allocate(dxdevice& dev, const builder& builder)
	{
		using restype = result<gpu_resource>;

		const bool is_buffer = builder.m_type == gpu_resource::type::buffer;
		if (!is_buffer && (builder.m_sizes.x == 0 || builder.m_sizes.y == 0 || builder.m_sizes.z == 0))
			return restype::make_fail("invalid size for a texture resource!");

		D3D12_HEAP_PROPERTIES heap_props{};
		heap_props.Type = builder.m_heap_type;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		if (!is_buffer)
		{
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
			if (builder.m_sizes.y > 1) desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			if (builder.m_sizes.z > 1)
			{
				desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			}
		}

		desc.Width = is_buffer ? builder.m_bytesize : builder.m_sizes.x;
		desc.Height = is_buffer ? 1 : builder.m_sizes.y;
		desc.DepthOrArraySize = is_buffer ? 1 : builder.m_sizes.z;
		desc.MipLevels = builder.m_num_mips;
		desc.SampleDesc.Count = 1;
		desc.Layout = is_buffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;

		// When D3D12_RESOURCE_DESC::Layout is D3D12_TEXTURE_LAYOUT_ROW_MAJOR, the D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL flag cannot be set
		if (builder.m_create_flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
			|| builder.m_create_flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			|| builder.m_create_flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		desc.Flags = builder.m_create_flags;
		desc.Format = builder.m_format;

		D3D12_CLEAR_VALUE* clear_value = nullptr;
		if (builder.m_has_clear_value)
		{
			// this is a pretty nasty way of doing this, but I'd rather just not allocate on heap :)
			// just never call this with 2 threads and we're fine.
			static D3D12_CLEAR_VALUE clear{}; clear = {};
			clear.Format = desc.Format;
			memcpy(clear.Color, &builder.m_clear_value_color, sizeof(float4));
			clear.DepthStencil.Depth = builder.m_clear_value_depth;
			clear.DepthStencil.Stencil = builder.m_clear_value_stencil;
			clear_value = &clear;
		}

		dxresource* resource;
		HRESULT hres = dev.CreateCommittedResource(
			&heap_props,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			builder.m_init_state,
			clear_value,
			IID_PPV_ARGS(&resource)
		);

		gpu_resource result{};
		result.m_builder = builder;
		result.m_resource = resource;
		result.m_current_state = builder.m_init_state;
		result.m_previous_state = D3D12_RESOURCE_STATE_COMMON;
		if (!SUCCEEDED(hres))
		{
			return restype::make_fail("");
		}
		else return restype::make_success(result);
	}

	bool is_valid() const { return m_resource != nullptr; }

	bool buffer_needs_realloc(const uint64 req_bytesize) const
	{
		return !is_valid() || m_resource->GetDesc().Width < req_bytesize;
	}

	bool get_dxdesc(D3D12_RESOURCE_DESC& out_desc)
	{
		if (is_valid())
		{
			out_desc = m_resource->GetDesc(); return true;
		}
		else return false;
	}
	dxresource* m_resource = nullptr;
	builder m_builder;
	dxresource_state m_previous_state;
	dxresource_state m_current_state;
};

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

template <typename _t, typename _fn>
result<> map_resource(gpu_resource& resource, _fn&& func)
{
	return map_resource<_t>(*resource.m_resource, func);
}


class renderman final
{
	dxfactory* m_factory;
	dxdevice* m_device;
	dxqueue* m_queue;
	dxadapter* m_adapter;
	dxallocator* m_cmd_allocator;
	dxcmdlist* m_cmdlist;
	dxfence* m_frame_fence;
	uint64 m_frame;
	shaderman m_shaderman;

	struct shadowmap
	{
		gpu_resource m_resource;
		descriptor m_dsv;
		descriptor m_srv;
	};
	shadowmap m_shadows;

	struct swapchain final
	{
		dxswapchain* m_swapchain;
		gpu_resource m_buffers[k_num_swapchain_buffers];
		gpu_resource m_depth;
		descriptor m_rtvs[k_num_swapchain_buffers];
		descriptor m_dsv;
		gpu_resource m_uav_proxy_resource;
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
		gpu_resource& get_current_backbuffer_resource()
		{
			return m_buffers[get_current_backbuffer_idx() % k_num_swapchain_buffers];
		}
	};
	vector<swapchain> m_swapchains{};
	umap<void*, uint32> m_swapchain_lookup{};

	struct descheap final
	{
		dxdescheap* m_dxheap;
		uint32 m_handle_size;
		uint32 m_stack_ptr;
		descriptor_heap::slot m_slot;

		result<uint32> allocate()
		{
			using restype = result<uint32>;
			if (m_stack_ptr >= descriptor_heap::capacity(m_slot))
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
			if (out_gpu_handle && descriptor_heap::is_gpu_heap(m_slot))
			{
				*out_gpu_handle = (D3D12_GPU_DESCRIPTOR_HANDLE)(m_dxheap->GetGPUDescriptorHandleForHeapStart().ptr + (id * m_handle_size));
			}
			return {};
		}
	};
	descheap m_descheaps[descriptor_heap::num];
	descheap& get_descheap(descriptor_heap::slot slt)
	{
		m_descheaps[(int)slt].m_slot = slt;
		return m_descheaps[(int)slt];
	}

	enum pipeline
	{
		cpip_skinning,
		cpip_num,
		pip_shadows,
		pip_shading,
		pip_wireframe,
		pip_lines,
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
		gpu_resource m_vertex_stagingbuffer;
		gpu_resource m_index_stagingbuffer;
		gpu_resource m_vertbuffer;
		gpu_resource m_indbuffer;
		D3D12_VERTEX_BUFFER_VIEW m_vtb_view;
		D3D12_INDEX_BUFFER_VIEW m_idx_view;
		bool m_uploaded = false;

		uint32 get_num_vertices() const { return m_vertbuffer.is_valid() ? (uint32)(m_vertbuffer.m_resource->GetDesc().Width / sizeof(gpu_vertex)) : 0; }
		uint32 get_num_indices() const { return m_indbuffer.is_valid() ? (uint32)(m_indbuffer.m_resource->GetDesc().Width / sizeof(uint32)) : 0; }
	};
	umap<mesh_id, meshbuffers> m_mesh_buffers;
	gpu_resource m_instancebuffer;
	gpu_resource m_instancebuffer_ui;
	gpu_resource m_instancebuffer_lines;
	descriptor m_instancebuffer_srv;
	descriptor m_instancebuffer_ui_srv;
	descriptor m_instancebuffer_lines_srv;
	
	struct cbuffers final
	{
		vector<gpu_resource> m_resources[cbuffer::num];
		vector<descriptor> m_cbvs[cbuffer::num];

		void ensure_allocated(renderman& owner, cbuffer::slot slot, uint32 idx);
		uint32 num(cbuffer::slot slot) const;
		gpu_resource& resource(cbuffer::slot slot, uint32 idx = 0u);
		descriptor* cbv(cbuffer::slot slot, uint32 idx = 0u);

		template <typename _fn>
		void write_data(cbuffer::slot slot, uint32 idx, _fn&& func)
		{
			map_resource<void>(resource(slot, idx), [&func](void* dest) { func(dest); });
		}
	};
	cbuffers m_cbuffers;
	
	dxresource* m_bonebuffer;
	descriptor m_bonebuffer_srv;

	struct skeletonbuffers final
	{
		gpu_resource m_bone_buffer;			// static bone data from content
		gpu_resource m_bone_buffer_staging;	// static bone data from content
		gpu_resource m_skinned_buffer;		// output of the GPU skinning
		bool m_uploaded = false;
	};
	umap<skel_id, skeletonbuffers> m_skel_buffers;

	struct texture
	{
		gpu_resource m_staging_resource;
		gpu_resource m_gpu_resource;
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
	void cmd_transition_barrier(gpu_resource& resource, D3D12_RESOURCE_STATES after);

	void clear_gpu_heaps();

	bool push_gpu_resource_descriptor(
		dxdevice& device, 
		const descriptor& source_descriptor,
		uint32& out_gpu_heap_index,
		D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle = nullptr);

	void render(renderscene& scene, const contentman& contentman);
	void render_shadows(renderscene& scene, const contentman& contentman);

	result<descriptor> create_resource_descriptor(dxresource& resource, const descriptor::builder& builder);
	result<descriptor> create_resource_descriptor(gpu_resource& resource, const descriptor::builder& builder)
	{
		return create_resource_descriptor(*resource.m_resource, builder);
	}

	result<> register_window(void* platform_handle);

private:
	void process_scene(renderscene& scene, const contentman& cman);
	void upload_buffers();
	void reallocate_image_texture(const contentman& cman, image_id id);
	void reallocate_skeleton_buffers(const contentman& cman, skel_id id);
	static void populate_vertex_shader_input(renderman::pipeline_desc& pipeline);
};
}