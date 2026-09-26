# Sodium (SDLL) — SoDium LossLess

Target: Minecraft Bedrock 1.26.50 Android / LeviLauncher..

## v0.2.0

- Native Preloader mod.
- Bundled resource pack injected through `resources/minecraft_resource_packs`.
- Vanilla Pack Settings with Frame Generation and Frame Multiplier (1x–3x).
- GLES frame capture using `glCopyTexSubImage2D`.
- First frame-generation path: linear interpolation between the previous and current rendered frame.
- 2x inserts one intermediate frame; 3x inserts two.
- No Levi Mod Menu dependency.

The native renderer currently starts from `frame_multiplier=2` in the Preloader config file. The vanilla Pack Settings UI is bundled now; connecting its selected value directly to the native renderer is the next bridge, because Pack Settings are exposed to resource-pack Molang rather than as a native C++ setting API.

## Termux

Place the project at:

`~/storage/downloads/injection/Sodium-SDLL`

Then use the Android NDK/xmake toolchain from your BedrockTools setup:

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c
xmake -y
```

The build packages the native library and bundled resource pack into `Sodium-SDLL.levipack`.


## Fast test build (0.3.0)
This revision focuses on making the native mod load reliably when Android loads `libEGL.so`. The runtime watches `dlopen()` and installs the EGL frame hook after EGL becomes available. The resource-pack/UI experiment was removed from the package because it is not required for native loading.

Target test: Minecraft Bedrock 1.26.51 ARM64.
