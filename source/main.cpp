#include "common.h"
#include "windowman.h"
#include "renderman.h"
#include "contentman.h"
#include "gameman.h"
#include "inputman.h"

using namespace strikers;

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\"; }

int main()
{
    windowman wman{};
    window_create_args window_args{};
    window_args.m_title = "strikers";
    window_args.m_size = { 1280, 720 };
    window_id wid = wman.new_window(window_args).claim();

    // initialize content
    contentman cman{};
    // cman.scan_assets_in_folder(k_content_folder);
    cman.load_fbx(string(k_content_folder) + "scene.fbx").claim();

    // initialize the renderer
    renderman rman{};
    rman.initialize().claim();
    rman.compile_shaders();

    gameman gman{};
    gman.start(cman);

    const platform::window_handle wplatform_handle = wman.get_window_platform_handle(wid).claim();
    rman.register_window(wplatform_handle).claim();

    float time = 0.0f;
    while (true)
    {
        const float delta_time = 0.1f;
        time += delta_time;

        wman.poll_windows();
        
        inputman::get().tick();
        gman.tick(time, delta_time);

        renderscene scene;
        gman.build_renderscene(cman, scene);
        rman.render(scene, cman);
    }
}