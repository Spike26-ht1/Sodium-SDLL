add_rules("mode.debug", "mode.release")
set_policy("package.requires_lock", true)

package("preloader")
    set_homepage("https://github.com/LiteLDev/preloader-android")
    set_description("Preloader Android")
    add_urls("https://github.com/LiteLDev/preloader-android.git")
    add_versions("main", "main")
    add_deps("cmake")
    on_install("android", function (package)
        local ndk = os.getenv("ANDROID_NDK_HOME") or os.getenv("ANDROID_NDK_ROOT")
        assert(ndk, "ANDROID_NDK_HOME/ANDROID_NDK_ROOT is required")

        local toolchain = path.join(ndk, "build", "cmake", "android.toolchain.cmake")

        local configs = {
            "-DCMAKE_TOOLCHAIN_FILE=" .. toolchain,
            "-DANDROID_ABI=arm64-v8a",
            "-DANDROID_PLATFORM=android-21"
        }

        import("package.tools.cmake").install(package, configs)
    end)
package_end()

add_requires("preloader")

target("SodiumSDLL")
    set_kind("shared")
    set_languages("c++20")
    set_strip("all")

    add_files("src/main.cpp", "src/core/*.cpp", "src/render/*.cpp")
    add_includedirs("include", {public = true})
    add_includedirs("src")
    add_packages("preloader")

    if is_plat("android") then
        add_cxflags("-fPIC", "-Oz", "-ffunction-sections", "-fdata-sections", "-flto", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fmerge-all-constants", "-fno-stack-protector", "-fexceptions", "-w", "-fvisibility=hidden")
        add_cxxflags("-fno-rtti", "-fvisibility-inlines-hidden")
        add_shflags("-Wl,--gc-sections", "-Wl,--icf=all", "-flto", "-Wl,--hash-style=gnu", "-Wl,-z,max-page-size=16384")
        add_links("android", "log", "EGL", "GLESv3", "GLESv2")
    end

    after_build(function (target)
        if not target:is_plat("android") then return end
        import("lib.detect.find_tool")
        local python = find_tool("python3") or find_tool("python")
        assert(python, "Python 3 is required to package Sodium (SDLL).levipack")

        local args = {
            path.join(os.projectdir(), "scripts", "package_levipack.py"),
            "--library", target:targetfile(),
            "--icon", path.join(os.projectdir(), "assets", "icon.png"),
            "--version-header", path.join(os.projectdir(), "include", "sodium", "Version.hpp"),
            "--resource-pack", path.join(os.projectdir(), "resources", "minecraft_resource_packs", "sodium"),
            "--output", path.join(target:targetdir(), "Sodium-SDLL.levipack")
        }

        os.vrunv(python.program, args)
    end)
