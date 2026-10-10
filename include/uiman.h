#pragma once

struct ImGuiContext;
namespace strikers {
class renderscene;
class contentman;
class gameman;

class uiman final
{
	ImGuiContext* m_ctx = nullptr;
public:
	void initialize(contentman& cman);
	void build_renderscene(
		const float delta_seconds,
		contentman& cman,
		gameman& gman,
		renderscene& renderscene);
};
}