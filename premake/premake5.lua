workspace "strikers"
    location "../generated"
    configurations { "debug", "release" }
    language "C++"
    architecture "x64"
    multiprocessorcompile("on")
    platforms { "win64" }

project "strikers"
    kind        "ConsoleApp"
    language    "C++"
    cppdialect  "C++20"
    targetdir   "../bin/%{cfg.buildcfg}_%{cfg.platform}"
    objdir      "../int/%{cfg.buildcfg}_%{cfg.platform}"
    basedir     "../"

    local includeDir = "../include/"
    local sourceFiles = {
        "../source/**.c",
        "../source/**.cpp"
    }
    local headerFiles = {
        includeDir .. "/**.h",
        includeDir .. "/**.hpp"
    }
    local shaderFiles = {
        "../hlsl/**.hlsl",
        "../hlsl/**.hlsli"
    }
    files(sourceFiles)
    files(headerFiles)
    -- files(shaderFiles)
    vpaths {
        ["source/*"] = sourceFiles,
        ["include/*"] = headerFiles,
        ["hlsl/*"] = shaderFiles
    }
    includedirs {
        includeDir,
        "../thirdparty/glm/include",
        "../thirdparty/assimp/include",
        "../thirdparty/pix/include",
        "../thirdparty/stb/include",
        "../thirdparty/box3d/include",
    }
    libdirs {
        "../thirdparty/dxc/lib/",
        "../thirdparty/assimp/lib/",
        "../thirdparty/pix/lib/",
        "../thirdparty/box3d/lib/",
    }

    defines{"DF_PIX"}
    filter "files:**.hlsl"
        buildaction "None"
    filter {}

    filter "platforms:win64"
        defines {"DF_WINDOWS"}
    filter{}
    filter "platforms:linux"
        defines {"DF_LINUX"}
    filter{}
    filter "configurations:debug"
       defines { "DF_DEBUG" }
       symbols "On"
    filter{}
    filter "configurations:release"
       defines { "DF_RELEASE" }
       optimize "On"
    filter{}