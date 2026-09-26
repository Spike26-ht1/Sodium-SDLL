#pragma once

#include <cstddef>

namespace sodium::core {

struct HookState {
    void* target = nullptr;
    void* detour = nullptr;
};

bool installHook(void* target, void* detour, void** original, HookState& state);
void removeHook(HookState& state);

}
