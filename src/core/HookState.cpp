#include "HookState.hpp"

#include <pl/memory/Hook.hpp>

namespace sodium::core {

bool installHook(void* target, void* detour, void** original, HookState& state) {
    if (!target || !detour || !original) return false;
    if (pl::memory::hook(target, detour, original) != 0) return false;

    state.target = target;
    state.detour = detour;
    return true;
}

void removeHook(HookState& state) {
    if (!state.target || !state.detour) return;
    pl::memory::unhook(state.target, state.detour);
    state = {};
}

}
