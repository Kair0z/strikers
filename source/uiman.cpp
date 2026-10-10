#include "common.h"
#include "uiman.h"
#include "renderman.h"
#include "commands.h"
#include "inputman.h"
#include "gameman.h"

#include "imgui.h"

namespace strikers {

float2 from_imgui(ImVec2 vec) {
	return float2(vec.x, vec.y);
}

static const image_id k_imgui_image = contentman::make_image_id("/imgui/");

void uiman::initialize(contentman& cman)
{
	m_ctx = ImGui::CreateContext();

	// imgui backend init:
	{
		auto& io = ImGui::GetIO();
		io.BackendRendererUserData = (void*)this;
		io.BackendRendererName = "strikers";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
		io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // We can honor ImGuiPlatformIO::Textures[] requests during render.
		
		io.BackendPlatformUserData = (void*)this;
		io.BackendPlatformName = "strikers_win32";
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // We can honor GetMouseCursor() values (optional)
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos; // We can honor io.WantSetMousePos requests (optional, rarely used)

		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		// platform_io.DrawCallback_ResetRenderState = ImGui_ImplDX12_DrawCallback_ResetRenderState;
		// platform_io.DrawCallback_SetSamplerLinear = ImGui_ImplDX12_DrawCallback_SetSamplerLinear;
		// platform_io.DrawCallback_SetSamplerNearest = ImGui_ImplDX12_DrawCallback_SetSamplerNearest;
	}
}

void uiman::build_renderscene(
	float delta_seconds,
	contentman& cman, 
	gameman& gman,
	renderscene& renderscene)
{
	static float seconds = 0.0f;
	seconds += delta_seconds;

	const float scale = remap(glm::sin(seconds * 5), 0, 1, 0.08f, 0.09f);
	renderscene.add_text_instance()
		.font(contentman::make_font_id(string(k_content_folder) + "fonts/chicago_athletic.ttf"))
		//.text(cm_test_text.value())
		.text(strikers::format("{}-{}", gman.score(1), gman.score(0)))
		.transform(calculate_transform(float2(-0.8f, -0.5f), 0.0f, float2(scale, scale)))
		.spacing({ 0.07f, 0 })
		.color(colors::white())
		.color('i', colors::red())
	;

	if (!cm_ui_enabled.enabled())
		return;

	auto& io = ImGui::GetIO();
	io.DisplaySize = { 1280, 720 };

	// input handling
	inputman& input = inputman::get();
	const auto& mousepos = input.get_mouse_position();
	io.AddMousePosEvent(mousepos.x, mousepos.y);
	io.AddMouseButtonEvent(0, input.is_button_down(inputman::button::lmouse));
	io.AddMouseButtonEvent(1, input.is_button_down(inputman::button::rmouse));

	// UI rendering
	ImGui::NewFrame();
	ImGui::Begin("test");
	gman.on_imgui();
	ImGui::End();
	ImGui::EndFrame();
	ImGui::Render();
	
	// UI into our renderer
	renderscene.m_imgui_mesh.clear();
	renderscene.m_imgui_mesh.m_font_image = k_imgui_image;
	if (ImGui::GetDrawData() != nullptr)
	{
		// load the font into an image
		static bool once = true;
		if (once)
		{
			image_asset::desc image_desc{};
			image_desc.m_num_channels = 4;
			unsigned char* pixeldata = nullptr;
			int width, height, bpp;
			io.Fonts->GetTexDataAsRGBA32(&pixeldata, &width, &height, &bpp);
			image_desc.m_size.x = width;
			image_desc.m_size.y = height;
			cman.load_raw_image(image_desc, pixeldata, k_imgui_image);
			once = false;
		}

		const auto& drawdata = *ImGui::GetDrawData();
		float L = drawdata.DisplayPos.x;
		float R = drawdata.DisplayPos.x + drawdata.DisplaySize.x;
		float T = drawdata.DisplayPos.y;
		float B = drawdata.DisplayPos.y + drawdata.DisplaySize.y;
		float mvp[4][4] =
		{
			{ 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
			{ 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
			{ 0.0f,         0.0f,           0.5f,       0.0f },
			{ (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
		};
		memcpy(&renderscene.m_imgui_mesh.m_projection, mvp, sizeof(mvp));

		auto& vertexbuffer = renderscene.m_imgui_mesh.m_vertices;
		auto& indexbuffer = renderscene.m_imgui_mesh.m_indices;
		for (const ImDrawList* draw_list : drawdata.CmdLists)
		{
			const uint32 start_vertex = (uint32)vertexbuffer.size();
			const uint32 start_index = (uint32)indexbuffer.size();
			renderscene.m_imgui_mesh.m_mesh_vertex_starts.push_back(start_vertex);
			renderscene.m_imgui_mesh.m_mesh_index_starts.push_back(start_index);
			renderscene.m_imgui_mesh.m_mesh_num_instances.push_back(1);
			renderscene.m_imgui_mesh.m_mesh_images;

			const uint32 num_vertices = draw_list->VtxBuffer.Size;
			vertexbuffer.resize(vertexbuffer.size() + num_vertices);
			for (uint32 v = 0; v < num_vertices; ++v) {
				vertexbuffer[start_vertex + v].color = draw_list->VtxBuffer[v].col;
				vertexbuffer[start_vertex + v].position = from_imgui(draw_list->VtxBuffer[v].pos);
				vertexbuffer[start_vertex + v].uv = from_imgui(draw_list->VtxBuffer[v].uv);
			}

			const uint32 num_indices = draw_list->IdxBuffer.Size;
			indexbuffer.resize(indexbuffer.size() + num_indices);
			for (uint32 i = 0u; i < num_indices; ++i) {
				indexbuffer[start_index + i] = draw_list->IdxBuffer[i];
			}
		}
	}
}
}