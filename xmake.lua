add_rules("mode.debug", "mode.release")
set_policy("package.requires_lock", true)

-- Preloader is built separately for Android in CI.
-- Keeping it out of xmake packages avoids host-platform resolution.
local preloader_root = os.getenv("PRELOADER_ROOT")

if is_plat("android") and not preloader_root then
    raise("PRELOADER_ROOT is required for Android builds")
end
target("SodiumSDLL")
    set_kind("shared")
    set_languages("c++20")
    set_strip("all")

    add_files("src/main.cpp", "src/core/*.cpp", "src/render/*.cpp")
    add_includedirs("include", {public = true})
    add_includedirs("src")

    if is_plat("android") then
        add_includedirs(
            path.join(preloader_root, "include"),
            {public = true}
        )

        add_linkdirs(path.join(preloader_root, "lib"))
        add_links("preloader")

        add_cxflags(
            "-fPIC",
            "-Oz",
            "-ffunction-sections",
            "-fdata-sections",
            "-flto",
            "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables",
            "-fmerge-all-constants",
            "-fno-stack-protector",
            "-fexceptions",
            "-w",
            "-fvisibility=hidden"
        )

        add_cxxflags(
            "-fno-rtti",
            "-fvisibility-inlines-hidden"
        )

        add_shflags(
            "-Wl,--gc-sections",
            "-Wl,--icf=all",
            "-flto",
            "-Wl,--hash-style=gnu",
            "-Wl,-z,max-page-size=16384"
        )

        add_links(
            "android",
            "log",
            "EGL",
            "GLESv3",
            "GLESv2"
        )
    end

    after_build(function (target)
        if not target:is_plat("android") then
            return
        end

        import("lib.detect.find_tool")

        local python = find_tool("python3") or find_tool("python")

if not python then
    raise("Python 3 is required to package Sodium (SDLL).levipack")
        end

        local args = {
            path.join(
                os.projectdir(),
                "scripts",
                "package_levipack.py"
            ),

            "--library",
            target:targetfile(),

            "--icon",
            path.join(
                os.projectdir(),
                "assets",
                "icon.png"
            ),

            "--version-header",
            path.join(
                os.projectdir(),
                "include",
                "sodium",
                "Version.hpp"
            ),

            "--resource-pack",
            path.join(
                os.projectdir(),
                "resources",
                "minecraft_resource_packs",
                "sodium"
            ),

            "--output",
            path.join(
                target:targetdir(),
                "Sodium-SDLL.levipack"
            )
        }

        os.vrunv(python.program, args)
    end)
