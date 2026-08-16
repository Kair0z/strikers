#pragma once

// STL
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <unordered_map>
#include <filesystem>
#include <unordered_set>
#include <format>
#include <fstream>
#include <optional>
#include <chrono>

// windows
#if DF_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// linux
#if DF_LINUX
#endif

// GLM
#include "glm/glm.hpp"
#include "glm/matrix.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/matrix_clip_space.hpp"
namespace strikers {

// constants
static const char* k_content_folder = "D:/Git/strikers/content/";

// common types
using int32 = int;
using f32	= float;
using uint8 = unsigned char;
using uint32 = uint32_t;
using uint64 = uint64_t;

static constexpr uint32 min(const uint32 a, const uint32 b)
{
	return a < b ? a : b;
}
static constexpr uint32 max(const uint32 a, const uint32 b)
{
	return a > b ? a : b;
}

using wstring = std::wstring;
using string = std::string;
using stringview = std::string_view;
inline wstring to_wstring(const stringview& view)
{
	if (view.empty()) return L"";
	int size_needed = MultiByteToWideChar(
		CP_UTF8, 0,
		view.data(), (int)view.size(),
		nullptr, 0
	);

	std::wstring result(size_needed, 0);
	MultiByteToWideChar(
		CP_UTF8, 0,
		view.data(), (int)view.size(),
		result.data(), size_needed
	);
	return result;
}
inline wstring to_wstring(const string& str)
{
	return to_wstring(stringview(str));
}

inline string to_lowercase(const string& str)
{
	string result = str;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
	return result;
}
inline string normalize_path(const stringview& filepath)
{
	std::filesystem::path p = filepath;
	return to_lowercase(p.generic_string());
}

template <typename _t>
_t snap_if_tiny(const _t& value)
{
	static constexpr _t eps = 0.00001;
	return value < eps ? (_t)0 : value;
}

#define format_f3 "{:.1f},{:.1f},{:.1f}"

template <typename _t>
using option = std::optional<_t>;

using mat4x4 = glm::mat4;
using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;
using float2 = glm::fvec2;
using float3 = glm::fvec3;
using float4 = glm::fvec4;
using float4x4 = glm::mat4;
using rotation = glm::quat;
static mat4x4 calculate_view_mat(const mat4x4& camera_transform)
{
	return glm::inverse(camera_transform);
}
static mat4x4 calculate_proj_mat(const float fov, const float aspect, const float plane_near, const float plane_far)
{
	return glm::perspectiveLH_ZO(
		fov,
		aspect,
		plane_near,
		plane_far);
}
static mat4x4 calculate_transform(const float3& position, const float3& lookAt, const float3& up = { 0,1,0 })
{
	mat4x4 view = glm::lookAtLH(position, lookAt, up);
	return glm::inverse(view);
}
static mat4x4 calculate_transform(const float3& position, const rotation& rot, const float3& scale)
{
	return glm::translate(glm::mat4(1.0f), position)
		* glm::mat4(rot)
		* glm::scale(glm::mat4(1.0f), scale);
}
template <typename _t>
inline static _t clamp_length(const _t& v, float max_length) 
{
	float len = glm::length(v);
	if (len > max_length && len > 0.0f)
	{
		return glm::normalize(v) * max_length;
	}
	return v;
}

struct transform final
{
public:
	mat4x4 m_matrix{ 1 };

	void add_rotation_camera(float yaw_delta, float pitch_delta)
	{
		constexpr float sensitivity = 0.002f;
		const float yaw = yaw_delta;
		const float pitch = pitch_delta;

		// World-space yaw.
		const glm::mat4 yaw_mat =
			glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));

		glm::mat3 orientation(m_matrix);
		orientation = glm::mat3(yaw_mat) * orientation;

		// Local-space pitch.
		const glm::mat4 pitch_mat =
			glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1, 0, 0));

		orientation = orientation * glm::mat3(pitch_mat);

		// Put the orientation back, preserving world position.
		m_matrix[0] = glm::vec4(orientation[0], 0.0f);
		m_matrix[1] = glm::vec4(orientation[1], 0.0f);
		m_matrix[2] = glm::vec4(orientation[2], 0.0f);
	}

	void add_position_local(const float3& delta)
	{
		m_matrix = glm::translate(m_matrix, delta);
	}
	void add_position_world(const float3& delta)
	{
		m_matrix = glm::translate(float4x4(1), delta) * m_matrix;
	}
	void add_euler_rotation_local(const float3& delta_eulers)
	{
		const glm::quat delta = glm::quat(glm::radians(delta_eulers));
		m_matrix *= glm::mat4_cast(delta);
	}
	void add_euler_rotation_world(const float3& delta_eulers)
	{
		const glm::quat delta = glm::quat(glm::radians(delta_eulers));
		m_matrix = glm::mat4_cast(delta) * m_matrix;
	}
	void set_position(const float3& position)
	{
		m_matrix[3] = float4(position, 1.0f);
	}
	void look_at(const float3& position)
	{
		m_matrix = calculate_transform(get_position(), position);
	}
	void set_scale(const float uniform)
	{
		set_scale(float3(uniform, uniform, uniform));
	}
	void set_scale(const float3& scale)
	{
		m_matrix[0] = glm::normalize(m_matrix[0]) * scale.x;
		m_matrix[1] = glm::normalize(m_matrix[1]) * scale.y;
		m_matrix[2] = glm::normalize(m_matrix[2]) * scale.z;
	}
	void set_rotation(const rotation& rotation)
	{
		const glm::vec3 position = glm::vec3(m_matrix[3]);
		const glm::vec3 scale{
			glm::length(glm::vec3(m_matrix[0])),
			glm::length(glm::vec3(m_matrix[1])),
			glm::length(glm::vec3(m_matrix[2]))
		};

		glm::mat4 rotation_matrix = glm::mat4_cast(rotation);

		rotation_matrix[0] *= scale.x;
		rotation_matrix[1] *= scale.y;
		rotation_matrix[2] *= scale.z;

		rotation_matrix[3] = glm::vec4(position, 1.0f);

		m_matrix = rotation_matrix;
	}
	float3 get_position() const
	{
		return m_matrix[3];
	}
	rotation get_rotation() const
	{
		glm::vec3 scale = glm::vec3(
			glm::length(glm::vec3(m_matrix[0])),
			glm::length(glm::vec3(m_matrix[1])),
			glm::length(glm::vec3(m_matrix[2]))
		);

		glm::mat3 rotationMatrix(
			glm::vec3(m_matrix[0]) / scale.x,
			glm::vec3(m_matrix[1]) / scale.y,
			glm::vec3(m_matrix[2]) / scale.z
		);

		return glm::quat_cast(rotationMatrix);
	}
	float3 get_scale() const
	{
		return glm::vec3(
			glm::length(glm::vec3(m_matrix[0])),
			glm::length(glm::vec3(m_matrix[1])),
			glm::length(glm::vec3(m_matrix[2]))
		);
	}
	float3 get_forward() const
	{
		return glm::normalize(m_matrix[2]);
	}
	float3 get_up() const
	{
		return glm::normalize(m_matrix[1]);
	}
	float3 get_right() const
	{
		return glm::normalize(m_matrix[0]);
	}
	static transform build(const float3& position, const rotation& rotation, const float3& scale)
	{
		transform trans{};
		trans.m_matrix = calculate_transform(position, rotation, scale);
		return trans;
	}
	static transform identity() { return transform{ mat4x4(1) }; }
};

template <typename _t> using vector = std::vector<_t>;
template <typename _k, typename _t, typename _h = std::hash<_k>, typename _eq = std::equal_to<_k>> 
using umap = std::unordered_map<_k, _t, _h, _eq>;

template <typename _fn>
using func = std::function<_fn>;

template <typename _k, typename _t> using uset = std::unordered_set<_k, _t>;
using id = uint64;
using scene_id = uint64;
using mesh_id = uint64;
using image_id = uint64;
using skel_id = uint64;
using anim_id = uint64;
using mat_id = uint64;
using camera_id = uint64;
static constexpr uint64 k_id_invalid = (id)-1;

template <typename _ex = int32, typename _unex = const char*>
class result final
{
	#define DF_CHECK_ON_CLAIM 1
	#define DF_CHECK_ON_MAKE 0
	#define DF_RESULT_CHECK								\
    do {												\
        if (m_flags == error) {							\
            fprintf(stderr, "result error: %s\n", m_unexpected); \
            abort();									\
        }												\
    } while (0);

	enum flags
	{
		success = 0,
		warning = (1 << 0),
		error = (1 << 1)
	};
	flags m_flags = success;
	union
	{
		_ex m_expected = {};
		_unex m_unexpected;
	};
	
public:
	result() = default;
	result(const result& other) = default;
	result(result&& other) = default;
	result(_ex&& ex) : m_expected{ ex }, m_flags{ success } {}
	result(const _ex& ex) : m_expected{ ex }, m_flags{ success } {}
	result(_unex&& unex) : m_unexpected{ unex }, m_flags{ error } 
	{ 
#if DF_CHECK_ON_MAKE
		DF_RESULT_CHECK;
#endif
	}
	result(const _unex& unex) : m_unexpected{ unex }, m_flags{ error } 
	{ 
#if DF_CHECK_ON_MAKE
		DF_RESULT_CHECK;
#endif
	}

	result& operator=(const result& other) = default;
	result& operator=(result&& other) = default;
	
	_ex& claim()
	{
#if DF_CHECK_ON_CLAIM
		DF_RESULT_CHECK
#endif
		return m_expected;
	}

	const _ex& claim() const
	{
#if DF_CHECK_ON_CLAIM
		DF_RESULT_CHECK
#endif
			return m_expected;
	}

	bool is_fail() const { return (m_flags & error) != 0; }
	bool is_warning() const { return (m_flags & warning) != 0; }
	bool is_success() const { return m_flags == 0; }

	static result make_success(const _ex& ex = {}) { return result(ex); }
	static result make_fail(_unex&& unex) { return result(unex); }
	static result make_warning(_unex&& unex)
	{
		result res{ unex };
		res.m_flags = warning;
		return res;
	}
};
}