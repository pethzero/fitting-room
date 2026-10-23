#include "NiOverrideLegacy.h"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace OS::NiOverrideLegacy {

    namespace {

        // ⚠⚠ SKSE64 1.5.97's BSFixedString, NOT CommonLib's, AT EVERY CALL
        // BOUNDARY. skse64 v2.0.20 GameTypes.h: `typedef StringCache::Ref
        // BSFixedString`, a struct holding one `const char*` with two
        // user-provided constructors and NO destructor, copy constructor or
        // assignment. MSVC x64 therefore passes it by value in a register (it
        // is trivially copyable) and returns it through a hidden pointer (it
        // has user constructors). RE::BSFixedString declares a copy constructor
        // and a destructor, so passing it would hand skee a pointer to a
        // temporary where it expects the string pointer itself. This mirror
        // keeps exactly the properties that decide the convention.
        //
        // ⚠ NO DESTRUCTOR ALSO MEANS skee NEVER RELEASES WHAT IT IS GIVEN. The
        // strings passed in are kept alive by an RE::BSFixedString held across
        // the call, and the pooled pointer is what goes over: the game's node
        // lookups compare BSFixedStrings by pointer, so an unpooled copy of
        // the same text would find nothing.
        struct SkseFixedString {
            SkseFixedString() noexcept {}
            explicit SkseFixedString(const char* a_data) noexcept : data(a_data) {}

            const char* data{ nullptr };
        };
        static_assert(sizeof(SkseFixedString) == 8);
        static_assert(std::is_trivially_copyable_v<SkseFixedString>);
        static_assert(!std::is_trivially_default_constructible_v<SkseFixedString>);

        // The first parameter of every static native is SKSE's StaticFunctionTag*,
        // which skee never reads.
        using Tag = void*;
        using Refr = RE::TESObjectREFR*;
        using U32  = std::uint32_t;

        using FnHasOverlays   = bool (*)(Tag, Refr);
        using FnAddOverlays   = void (*)(Tag, Refr);
        using FnCount         = U32 (*)(Tag);
        using FnHasNode       = bool (*)(Tag, Refr, bool, SkseFixedString, U32, U32);
        using FnAddNodeString = void (*)(Tag, Refr, bool, SkseFixedString, U32, U32, SkseFixedString, bool);
        using FnAddNodeInt    = void (*)(Tag, Refr, bool, SkseFixedString, U32, U32, U32, bool);
        using FnAddNodeFloat  = void (*)(Tag, Refr, bool, SkseFixedString, U32, U32, float, bool);
        using FnGetNodeString = SkseFixedString (*)(Tag, Refr, bool, SkseFixedString, U32, U32);
        using FnGetNodeInt    = U32 (*)(Tag, Refr, bool, SkseFixedString, U32, U32);
        using FnGetNodeFloat  = float (*)(Tag, Refr, bool, SkseFixedString, U32, U32);
        using FnRemoveNode    = void (*)(Tag, Refr, bool, SkseFixedString, U32, U32);
        using FnApplyNode     = void (*)(Tag, Refr);

        struct Natives {
            FnHasOverlays   hasOverlays{ nullptr };
            FnAddOverlays   addOverlays{ nullptr };
            FnCount         numBody{ nullptr };
            FnCount         numHand{ nullptr };
            FnCount         numFeet{ nullptr };
            FnCount         numFace{ nullptr };
            FnHasNode       hasNode{ nullptr };
            FnAddNodeString addString{ nullptr };
            FnAddNodeInt    addInt{ nullptr };
            FnAddNodeFloat  addFloat{ nullptr };
            FnGetNodeString getString{ nullptr };
            FnGetNodeInt    getInt{ nullptr };
            FnGetNodeFloat  getFloat{ nullptr };
            FnRemoveNode    removeNode{ nullptr };
            FnApplyNode     applyNode{ nullptr };
        };

        Natives           g_natives;
        std::atomic<bool> g_ready{ false };

        // Name, parameter count as Papyrus declares it (the tag is not one),
        // and where the resolved pointer goes. Counts are the NativeFunctionN
        // arities in skee's PapyrusNiOverride.cpp.
        struct Wanted {
            const char* name;
            std::uint32_t params;
            void**        slot;
        };

        template <typename Fn>
        void** Slot(Fn& a_fn) {
            return reinterpret_cast<void**>(&a_fn);
        }

        struct ImageRange {
            std::uintptr_t begin{ 0 };
            std::uintptr_t end{ 0 };

            [[nodiscard]] bool Contains(const void* a_ptr) const {
                const auto at = reinterpret_cast<std::uintptr_t>(a_ptr);
                return at >= begin && at < end;
            }
        };

        [[nodiscard]] ImageRange SkeeImage() {
            const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(L"skee64.dll"));
            if (!base) {
                return {};
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const auto  at  = reinterpret_cast<std::uintptr_t>(base);
            return { at, at + nt->OptionalHeader.SizeOfImage };
        }

        // ⚠ THE CALLBACK SITS RIGHT AFTER THE GAME'S NativeFunctionBase. SKSE's
        // NativeFunction adds one member, `void* m_callback`, after the 0x50
        // bytes it shares with the engine's NF_util::NativeFunctionBase (skse64
        // v2.0.20 PapyrusNativeFunctions.h; CommonLib asserts the 0x50). The
        // image check below is what stops a different layout from being called.
        [[nodiscard]] void* CallbackOf(RE::BSScript::IFunction* a_fn) {
            static_assert(sizeof(RE::BSScript::NF_util::NativeFunctionBase) == 0x50);
            const auto* bytes = reinterpret_cast<const std::byte*>(a_fn);
            return *reinterpret_cast<void* const*>(bytes + sizeof(RE::BSScript::NF_util::NativeFunctionBase));
        }

        [[nodiscard]] SkseFixedString Pass(const RE::BSFixedString& a_held) {
            return SkseFixedString{ a_held.data() };
        }

    }  // namespace

    bool Resolve() {
        if (g_ready.load(std::memory_order_acquire)) {
            return true;
        }
        const auto image = SkeeImage();
        if (!image.begin) {
            spdlog::warn("NiOverrideLegacy: skee64.dll is not loaded.");
            return false;
        }
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            spdlog::warn("NiOverrideLegacy: the Papyrus VM is not up yet.");
            return false;
        }
        RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> type;
        if (!vm->GetScriptObjectType(RE::BSFixedString{ "NiOverride" }, type) || !type) {
            spdlog::warn("NiOverrideLegacy: the NiOverride script is not available (is "
                         "RaceMenu's NiOverride.pex installed?).");
            return false;
        }

        Natives found;
        const Wanted wanted[] = {
            { "HasOverlays", 1, Slot(found.hasOverlays) },
            { "AddOverlays", 1, Slot(found.addOverlays) },
            { "GetNumBodyOverlays", 0, Slot(found.numBody) },
            { "GetNumHandOverlays", 0, Slot(found.numHand) },
            { "GetNumFeetOverlays", 0, Slot(found.numFeet) },
            { "GetNumFaceOverlays", 0, Slot(found.numFace) },
            { "HasNodeOverride", 5, Slot(found.hasNode) },
            { "AddNodeOverrideString", 7, Slot(found.addString) },
            { "AddNodeOverrideInt", 7, Slot(found.addInt) },
            { "AddNodeOverrideFloat", 7, Slot(found.addFloat) },
            { "GetNodeOverrideString", 5, Slot(found.getString) },
            { "GetNodeOverrideInt", 5, Slot(found.getInt) },
            { "GetNodeOverrideFloat", 5, Slot(found.getFloat) },
            { "RemoveNodeOverride", 5, Slot(found.removeNode) },
            { "ApplyNodeOverrides", 1, Slot(found.applyNode) },
        };

        const auto* funcs = type->GetGlobalFuncIter();
        const auto  count = type->GetNumGlobalFuncs();
        for (const auto& want : wanted) {
            for (std::uint32_t i = 0; i < count; ++i) {
                auto* fn = funcs ? funcs[i].func.get() : nullptr;
                if (!fn || std::strcmp(fn->GetName().c_str(), want.name) != 0) {
                    continue;
                }
                if (!fn->GetIsNative() || !fn->GetIsStatic() || fn->GetParamCount() != want.params) {
                    spdlog::error("NiOverrideLegacy: NiOverride.{} is not the native this build "
                                  "expects (native {}, static {}, {} params, want {}).",
                                  want.name, fn->GetIsNative(), fn->GetIsStatic(),
                                  fn->GetParamCount(), want.params);
                    return false;
                }
                void* callback = CallbackOf(fn);
                if (!image.Contains(callback)) {
                    spdlog::error("NiOverrideLegacy: NiOverride.{} does not lead into skee64.dll "
                                  "(0x{:X}), so it is not called.",
                                  want.name, reinterpret_cast<std::uintptr_t>(callback));
                    return false;
                }
                *want.slot = callback;
                break;
            }
            if (!*want.slot) {
                spdlog::error("NiOverrideLegacy: NiOverride.{} was not found.", want.name);
                return false;
            }
        }

        g_natives = found;
        g_ready.store(true, std::memory_order_release);
        spdlog::info("NiOverrideLegacy: {} NiOverride natives resolved inside skee64.dll.",
                     std::size(wanted));
        return true;
    }

    bool Ready() { return g_ready.load(std::memory_order_acquire); }

    bool HasOverlays(RE::TESObjectREFR* a_refr) {
        return Ready() && a_refr && g_natives.hasOverlays(nullptr, a_refr);
    }

    void AddOverlays(RE::TESObjectREFR* a_refr) {
        if (Ready() && a_refr) {
            g_natives.addOverlays(nullptr, a_refr);
        }
    }

    std::uint32_t NumBodyOverlays() { return Ready() ? g_natives.numBody(nullptr) : 0u; }
    std::uint32_t NumHandOverlays() { return Ready() ? g_natives.numHand(nullptr) : 0u; }
    std::uint32_t NumFeetOverlays() { return Ready() ? g_natives.numFeet(nullptr) : 0u; }
    std::uint32_t NumFaceOverlays() { return Ready() ? g_natives.numFace(nullptr) : 0u; }

    bool HasNodeOverride(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                         std::uint32_t a_key, std::uint32_t a_index) {
        if (!Ready() || !a_refr || !a_node) {
            return false;
        }
        const RE::BSFixedString node{ a_node };
        return g_natives.hasNode(nullptr, a_refr, a_female, Pass(node), a_key, a_index);
    }

    void AddNodeOverrideString(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                               std::uint32_t a_key, std::uint32_t a_index,
                               const std::string& a_value) {
        if (!Ready() || !a_refr || !a_node) {
            return;
        }
        const RE::BSFixedString node{ a_node };
        const RE::BSFixedString value{ a_value };
        g_natives.addString(nullptr, a_refr, a_female, Pass(node), a_key, a_index, Pass(value),
                            true);
    }

    void AddNodeOverrideInt(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                            std::uint32_t a_key, std::uint32_t a_index, std::uint32_t a_value) {
        if (!Ready() || !a_refr || !a_node) {
            return;
        }
        const RE::BSFixedString node{ a_node };
        g_natives.addInt(nullptr, a_refr, a_female, Pass(node), a_key, a_index, a_value, true);
    }

    void AddNodeOverrideFloat(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                              std::uint32_t a_key, std::uint32_t a_index, float a_value) {
        if (!Ready() || !a_refr || !a_node) {
            return;
        }
        const RE::BSFixedString node{ a_node };
        g_natives.addFloat(nullptr, a_refr, a_female, Pass(node), a_key, a_index, a_value, true);
    }

    std::string GetNodeOverrideString(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                                      std::uint32_t a_key, std::uint32_t a_index) {
        if (!Ready() || !a_refr || !a_node) {
            return {};
        }
        const RE::BSFixedString node{ a_node };
        const auto result = g_natives.getString(nullptr, a_refr, a_female, Pass(node), a_key, a_index);
        return result.data ? std::string{ result.data } : std::string{};
    }

    std::uint32_t GetNodeOverrideInt(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                                     std::uint32_t a_key, std::uint32_t a_index) {
        if (!Ready() || !a_refr || !a_node) {
            return 0u;
        }
        const RE::BSFixedString node{ a_node };
        return g_natives.getInt(nullptr, a_refr, a_female, Pass(node), a_key, a_index);
    }

    float GetNodeOverrideFloat(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                               std::uint32_t a_key, std::uint32_t a_index) {
        if (!Ready() || !a_refr || !a_node) {
            return 0.0f;
        }
        const RE::BSFixedString node{ a_node };
        return g_natives.getFloat(nullptr, a_refr, a_female, Pass(node), a_key, a_index);
    }

    void RemoveNodeOverride(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                            std::uint32_t a_key, std::uint32_t a_index) {
        if (!Ready() || !a_refr || !a_node) {
            return;
        }
        const RE::BSFixedString node{ a_node };
        g_natives.removeNode(nullptr, a_refr, a_female, Pass(node), a_key, a_index);
    }

    void ApplyNodeOverrides(RE::TESObjectREFR* a_refr) {
        if (Ready() && a_refr) {
            g_natives.applyNode(nullptr, a_refr);
        }
    }

}  // namespace OS::NiOverrideLegacy
