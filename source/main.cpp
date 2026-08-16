#include "common.h"
#include "windowman.h"
#include "renderman.h"
#include "contentman.h"
#include "gameman.h"
#include "inputman.h"
#include "commandman.h"
#include "physman.h"
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
    cman.load_fbx(string(k_content_folder) + "meshes/box.fbx").claim();

    physman& physics = physman::get();
    physics.initialize();

    // initialize the renderer
    renderman rman{};
    rman.initialize().claim();
    rman.compile_shaders();

    gameman gman{};
    gman.start(cman);

    const platform::window_handle wplatform_handle = wman.get_window_platform_handle(wid).claim();
    rman.register_window(wplatform_handle).claim();

    using timepoint = std::chrono::steady_clock::time_point;;
    timepoint last = std::chrono::steady_clock::now();
    timepoint now = last;
    float time = 0.0f;
    while (true)
    {   
        now = std::chrono::steady_clock::now();
        const float ms_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        const float delta_seconds = ms_since_last * 0.01f;
        last = now;
        time += delta_seconds;
        
        commandman::get().command_script("D:/Git/strikers/commands.md");
        wman.poll_windows();
        
        physics.tick();

        inputman::get().tick();
        gman.tick(time, delta_seconds);

        renderscene scene;
        gman.build_renderscene(cman, scene);
        rman.render(scene, cman);

        log_with_cooldown(delta_seconds, 1.0f, "[fps]{}", 1.0f / delta_seconds);
    }
}