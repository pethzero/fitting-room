#pragma once

#include <cstdint>
#include <string>

namespace RE {
    class TESObjectARMA;
    class TESObjectARMO;
    class TESObjectREFR;
}

// RaceMenu SE 0.4.16's node overrides and overlays, reached through its own
// Papyrus natives (the NiOverride script) instead of the C++ interfaces.
//
// ⚠⚠ WHY NOT THE C++ INTERFACES. skee64 0.4.16 (the last build for Skyrim SE
// 1.5.97) answers the interface exchange with Overlay v1, Override v1 and
// NiTransform v2. Those are NOT older revisions of the vtables in
// extern/RaceMenu/IPluginInterface.h: they are skee's implementation classes,
// with Save/Load in the middle of the table, an OverrideVariant instead of the
// SetVariant/GetVariant visitors, and std::function parameters. Worse, a string
// override's value is a std::shared_ptr out of skee's private string table, and
// skee serializes it by looking that pointer up in the table: one made on this
// side writes string id -1 into the co-save. Measured against skee source
// expired6978/SKSE64Plugins 87a5cad (2020-10-02, the commit 0.4.16 was built
// from; the interface headers are unchanged since 0.4.14) and checked against
// the shipped skee64.dll 4.0.0.0 of 2020-10-04 for every native name used here.
//
// ⚠ THE NATIVES ARE THE STABLE SURFACE. NiOverride.psc is RaceMenu's public
// modder API, its signatures are plain values (a reference, bools, ints, floats
// and BSFixedStrings), and AddNodeOverrideString interns its string in skee's
// own table, so what it writes saves and loads like anything RaceMenu wrote.
//
// ⚠ CALLED DIRECTLY, NOT DISPATCHED THROUGH THE VM. A VM call is queued and
// answers later; the Overlays page reads on the frame it draws. Each native is
// found by name in the NiOverride script's type info, its parameter count is
// checked, and the C++ function SKSE registered for it is taken from the
// NativeFunction object - and refused unless it lies inside skee64.dll's image.
namespace OS::NiOverrideLegacy {

    // Find every native this file calls. Game thread, with the Papyrus VM up
    // (kDataLoaded or later): it may load and link the NiOverride script.
    //
    // ⚠ ALL OR NOTHING. A missing or mismatched native leaves the whole set
    // unresolved, so a caller never runs half a write.
    [[nodiscard]] bool Resolve();
    [[nodiscard]] bool Ready();

    [[nodiscard]] bool HasOverlays(RE::TESObjectREFR* a_refr);
    void               AddOverlays(RE::TESObjectREFR* a_refr);

    // The overlay slot counts skee read from its INI (normal, not spell).
    [[nodiscard]] std::uint32_t NumBodyOverlays();
    [[nodiscard]] std::uint32_t NumHandOverlays();
    [[nodiscard]] std::uint32_t NumFeetOverlays();
    [[nodiscard]] std::uint32_t NumFaceOverlays();

    // Node overrides. ⚠ WHAT persist MEANS IN 0.4.16: true puts the override
    // in skee's store (and the co-save) AND applies it to the node at once;
    // false only applies it, which is IOverrideInterface::SetNodeProperty.
    // Either way the apply only happens when a_female matches the actor's sex.
    // The numeric writes always persist.
    [[nodiscard]] bool HasNodeOverride(RE::TESObjectREFR* a_refr, bool a_female,
                                       const char* a_node, std::uint32_t a_key,
                                       std::uint32_t a_index);
    void AddNodeOverrideString(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                               std::uint32_t a_key, std::uint32_t a_index,
                               const std::string& a_value, bool a_persist = true);
    void AddNodeOverrideInt(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                            std::uint32_t a_key, std::uint32_t a_index, std::uint32_t a_value);
    void AddNodeOverrideFloat(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                              std::uint32_t a_key, std::uint32_t a_index, float a_value);

    // ⚠ A MISSING OVERRIDE READS AS EMPTY OR ZERO, which is skee's own
    // behaviour; ask HasNodeOverride first when absence matters.
    [[nodiscard]] std::string   GetNodeOverrideString(RE::TESObjectREFR* a_refr, bool a_female,
                                                      const char* a_node, std::uint32_t a_key,
                                                      std::uint32_t a_index);
    [[nodiscard]] std::uint32_t GetNodeOverrideInt(RE::TESObjectREFR* a_refr, bool a_female,
                                                   const char* a_node, std::uint32_t a_key,
                                                   std::uint32_t a_index);
    [[nodiscard]] float         GetNodeOverrideFloat(RE::TESObjectREFR* a_refr, bool a_female,
                                                     const char* a_node, std::uint32_t a_key,
                                                     std::uint32_t a_index);

    void RemoveNodeOverride(RE::TESObjectREFR* a_refr, bool a_female, const char* a_node,
                            std::uint32_t a_key, std::uint32_t a_index);

    // Re-apply every node override the reference holds (deferred), the
    // counterpart of IOverrideInterface::SetNodeProperties(refr, false).
    void ApplyNodeOverrides(RE::TESObjectREFR* a_refr);

    // Armour-addon overrides, the same contract as the node ones. ⚠ persist
    // false is SetArmorProperty for BOTH persons at once: skee's
    // SetArmorAddonProperty visits the first- and third-person addon alike.
    [[nodiscard]] bool HasOverride(RE::TESObjectREFR* a_refr, bool a_female,
                                   RE::TESObjectARMO* a_armor, RE::TESObjectARMA* a_addon,
                                   const char* a_node, std::uint32_t a_key, std::uint32_t a_index);
    void AddOverrideString(RE::TESObjectREFR* a_refr, bool a_female, RE::TESObjectARMO* a_armor,
                           RE::TESObjectARMA* a_addon, const char* a_node, std::uint32_t a_key,
                           std::uint32_t a_index, const std::string& a_value, bool a_persist);
    void RemoveOverride(RE::TESObjectREFR* a_refr, bool a_female, RE::TESObjectARMO* a_armor,
                        RE::TESObjectARMA* a_addon, const char* a_node, std::uint32_t a_key,
                        std::uint32_t a_index);
    // Re-apply every armour override the reference holds (deferred), the
    // counterpart of SetArmorProperties(refr, false). ⚠ ALSO STANDS IN FOR
    // ApplyArmorOverrides(one addon): 0.4.16 exposes no per-addon apply as a
    // native, so a write at attach re-applies the whole actor, deferred.
    void ApplyOverrides(RE::TESObjectREFR* a_refr);

    // Node transform scale under a named key (NiTransform v2's natives).
    [[nodiscard]] bool  HasNodeTransformScale(RE::TESObjectREFR* a_refr, bool a_firstPerson,
                                              bool a_female, const char* a_node, const char* a_key);
    void                AddNodeTransformScale(RE::TESObjectREFR* a_refr, bool a_firstPerson,
                                              bool a_female, const char* a_node, const char* a_key,
                                              float a_scale);
    [[nodiscard]] float GetNodeTransformScale(RE::TESObjectREFR* a_refr, bool a_firstPerson,
                                              bool a_female, const char* a_node, const char* a_key);
    void                RemoveNodeTransformScale(RE::TESObjectREFR* a_refr, bool a_firstPerson,
                                                 bool a_female, const char* a_node,
                                                 const char* a_key);
    // UpdateNodeAllTransforms(refr).
    void UpdateAllReferenceTransforms(RE::TESObjectREFR* a_refr);

}  // namespace OS::NiOverrideLegacy
