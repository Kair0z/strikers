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
#include <stdlib.h>

// windows
#if DF_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// dx12
#include <d3d12.h>
#include <dxgi1_6.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxil.lib")

// pix
#if DF_PIX
#define USE_PIX 1
#include "WinPixEventRuntime/pix3.h"
#pragma comment(lib, "WinPixEventRuntime.lib")
#endif
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

// common types
using int32 = int;
using f32 = float;
using uint8 = unsigned char;
using uint32 = uint32_t;
using uint64 = uint64_t;

// constants
#define DF_FOLDER_CONTENT	"D:/Git/strikers/content/"
#define DF_FOLDER_SHADERS	"D:/Git/strikers/hlsl/"
#define DF_COMMANDS_SCRIPT	"D:/Git/strikers/commands.md"
#define DF_SETUP_SCRIPT		"D:/Git/strikers/setup.md"

static const char* k_content_folder = DF_FOLDER_CONTENT;
static const char* k_shaders_folder = DF_FOLDER_SHADERS;
static constexpr uint32 k_num_runners_per_team = 4;
static constexpr uint32 k_num_keepers_per_team = 1;

static constexpr uint32 k_num_swapchain_buffers = 3;

template <typename _t>
static constexpr _t k_invalid = (_t)-1;

template <typename _t>
static constexpr bool is_valid(const _t& val) { return val != k_invalid<_t>; }

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

template<typename... _args>
static string format(const stringview& fmt, _args&&... args)
{
	return std::vformat(fmt, std::make_format_args(args...));
}

template <typename _t>
_t snap_if_tiny(const _t& value)
{
	static constexpr _t eps = 0.00001;
	return value < eps ? (_t)0 : value;
}

inline float random_flt(const float min, const float max)
{
	return (float)rand() / (float)RAND_MAX * (max - min) + min;
}

#define format_f3 "{:.1f},{:.1f},{:.1f}"

template <typename _t>
using option = std::optional<_t>;
template <typename _t0, typename _t1>
using pair = std::pair<_t0, _t1>;

using mat4x4 = glm::mat4;
using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;
using float2 = glm::fvec2;
using float3 = glm::fvec3;
using float4 = glm::fvec4;
using float4x4 = glm::mat4;
using rotation = glm::quat;
using color = float4;

inline float2 lerp(const float2& a, const float2& b, float t)
{
	t = glm::clamp(t, 0.0f, 1.0f);
	return float2(std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t));
}

namespace colors
{
	static constexpr color red() { return float4(1, 0, 0, 1); }
	static constexpr color green() { return float4(0, 1, 0, 1); }
	static constexpr color blue() { return float4(0, 0, 1, 1); }
	static constexpr color white() { return float4(1, 1, 1, 1); }
	static constexpr color purple() { return float4(0.5f, 0.5f, 1.0f, 1.0f); }
}

struct sphere
{
	float4 m_position_radius;
	
	sphere() = default;
	sphere(float3 position, float radius) : m_position_radius{ position, radius } { }
	sphere(const float4 posrad) : m_position_radius{ posrad } {}

	static sphere unit()
	{
		return sphere(float3(0,0,0), 1.0f);
	}

	float radius() const { return m_position_radius.w; }
	float3 position() const { return float3(m_position_radius.x, m_position_radius.y, m_position_radius.z); }
};
struct box
{
	float3 m_position;
	float3 m_min;
	float3 m_max;

	box() = default;

	static box unit(float scale = 1.0f)
	{
		static box unitbox{};
		unitbox.m_min = -float3(1,1,1) * 0.5f * scale;
		unitbox.m_max = float3(1, 1, 1) * 0.5f * scale;
		unitbox.m_position = { 0,0,0 };
		return unitbox;
	}

	void grow_to_fit(const float3 point)
	{
		const float3 rel_point = point - m_position;
		m_min.x = glm::min(m_min.x, rel_point.x);
		m_min.y = glm::min(m_min.y, rel_point.y);
		m_min.z = glm::min(m_min.z, rel_point.z);
		m_max.x = glm::max(m_max.x, rel_point.x);
		m_max.y = glm::max(m_max.y, rel_point.y);
		m_max.z = glm::max(m_max.z, rel_point.z);
	}

	float3 abs_min() const
	{
		return m_position + m_min;
	}
	float3 abs_max() const
	{
		return m_position + m_max;
	}
};

struct collision final
{
	bool m_collided;
	float3 m_intersection_depth;
	float3 m_collision_normal;

	static bool calculate(const box& a, const box& b, collision* out_collision_info)
	{
		const float3& min_a = a.abs_min();
		const float3& max_a = a.abs_max();
		const float3& min_b = b.abs_min();
		const float3& max_b = b.abs_max();

		const float3 intersection_depth = {
			glm::min(max_a.x - min_b.x, max_b.x - min_a.x),
			glm::min(max_a.y - min_b.y, max_b.y - min_a.y),
			glm::min(max_a.z - min_b.z, max_b.z - min_a.z)
		};

		const bool collided = intersection_depth.x > 0.0f &&
			intersection_depth.y > 0.0f &&
			intersection_depth.z > 0.0f;

		if (out_collision_info)
		{
			const float3 center_delta = (min_a + max_a) - (min_b + max_b);

			(*out_collision_info).m_collided = collided;
			(*out_collision_info).m_intersection_depth = intersection_depth;
			(*out_collision_info).m_collision_normal = float3(
				intersection_depth.x <= intersection_depth.y && intersection_depth.x <= intersection_depth.z ? glm::sign(center_delta.x) : 0.0f,
				intersection_depth.y <= intersection_depth.x && intersection_depth.y <= intersection_depth.z ? glm::sign(center_delta.y) : 0.0f,
				intersection_depth.z <= intersection_depth.x && intersection_depth.z <= intersection_depth.y ? glm::sign(center_delta.z) : 0.0f
			);
		}

		return collided;
	}
};

static mat4x4 calculate_view_mat(const mat4x4& camera_transform)
{
	return glm::inverse(camera_transform);
}
static mat4x4 calculate_perspective_proj_mat(
	const float fov, 
	const float aspect, 
	const float plane_near, 
	const float plane_far)
{
	return glm::perspectiveLH_ZO(
		fov,
		aspect,
		plane_near,
		plane_far);
}
static mat4x4 calculate_orthographic_proj_mat(
	const float2& left_right,
	const float2& bottom_top,
	const float2& near_far)
{
	return glm::orthoLH_ZO(
		left_right.x,
		left_right.y,
		bottom_top.x,
		bottom_top.y,
		near_far.x,
		near_far.y);
}

static rotation make_look_rotation(const float3& forward, const float3& up = {0,1,0})
{
	return glm::quatLookAtLH(glm::normalize(forward), up);
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

enum class space
{
	world,
	local
};

struct transform final
{
public:
	mat4x4 m_matrix{ 1 };

	transform() = default;
	transform(const mat4x4& mat) : m_matrix{ mat } {}

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
	void look_twd(const float3& forward)
	{
		look_at(get_position() + forward);
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
template <typename _t> 
using uset = std::unordered_set<_t>;

using id = uint64;
using scene_id = uint64;
using mesh_id = uint64;
using image_id = uint64;
using skel_id = uint64;
using anim_id = uint64;
using mat_id = uint64;
using camera_id = uint64;
static constexpr uint64 k_id_invalid = (id)-1;

enum class logcolor
{
	white,
	red,
	blue,
	green,
	yellow,
	num
};

class logman final
{
	inline static int s_curr_lifetime = -1;
	inline static logcolor s_curr_color;
	inline static logcolor s_prev_color;

public:
	static void color(logcolor clr, int num_logs = -1)
	{
		s_curr_lifetime = num_logs;
		s_prev_color = s_curr_color;
		s_curr_color = clr;

#if DF_WINDOWS
		static const int k_color_remap[]
		{
			// https://cplusplus.com/forum/beginner/77879/
			15,
			4,
			9,
			10,
			14
		};
		::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), k_color_remap[(int)s_curr_color]);
#endif
	}
	static void prev_color()
	{
		color(s_prev_color);
	}

	template<typename... _args>
	static void log(const stringview& fmt, _args&&... args)
	{
		static bool first = true;
		if (first) color(s_curr_color), first = false;

		std::string formatted = "[strk] ";
		formatted += std::vformat(fmt, std::make_format_args(args...));
		formatted += "\n";
		std::cout << formatted;
		OutputDebugStringA(formatted.c_str());

		if (s_curr_lifetime)
		{
			--s_curr_lifetime;
			if (s_curr_lifetime == 0) prev_color();
		}
	}
};

#define on_cooldown(condition, delta, cd) \
	{ \
	static float s_timer = 0.0f; \
	s_timer -= delta;\
	bool is_tick = s_timer < 0; \
	if (is_tick && condition) s_timer = cd; \
	if (is_tick && condition)\

#define log_with_cooldown(delta, cd, mssg, ...) \
	{ \
	static float s_timer = 0.0f; \
	s_timer -= delta; \
	if (s_timer < 0.0f) logman::log(mssg, __VA_ARGS__), s_timer = cd;\
	}

template <typename _ex = int32, typename _unex = const char*>
class result final
{
	#define DF_CHECK_ON_CLAIM 1
	#define DF_CHECK_ON_MAKE 1
	#define DF_RESULT_CHECK								\
    do {												\
        if (m_flags == error) {							\
			logman::color(logcolor::red);				\
            logman::log("error: {}", m_unexpected);		\
			logman::prev_color();						\
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