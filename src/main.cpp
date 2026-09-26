#include "core/Runtime.hpp"

#include <pl/Mod.hpp>

class SodiumSDLLMod {
public:
    static SodiumSDLLMod& instance() {
        static SodiumSDLLMod mod;
        return mod;
    }

    bool load(pl::mod::ModContext& context) {
        return sodium::core::Runtime::instance().load(context);
    }

    bool enable(pl::mod::ModContext& context) {
        return sodium::core::Runtime::instance().enable(context);
    }

    bool disable(pl::mod::ModContext& context) {
        return sodium::core::Runtime::instance().disable(context);
    }

    bool unload(pl::mod::ModContext& context) {
        return sodium::core::Runtime::instance().unload(context);
    }
};

PL_REGISTER_MOD(SodiumSDLLMod, SodiumSDLLMod::instance())
