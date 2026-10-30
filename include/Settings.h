#pragma once

namespace Settings {
    // What a short tap of sprint does while sneaking and moving (SprintCancelsSneak.ini, iTapAction):
    // 0 = sneak roll (vanilla behavior), 1 = stand up and sprint,
    // 2 = stand up and sprint unless the player has the sneak roll perk, then roll.
    inline int tap_action = 0;
    // Silent Roll, the vanilla perk that enables the sneak roll
    constexpr RE::FormID roll_perk_id = 0x105F23;
    inline RE::BGSPerk* roll_perk = nullptr;

    void Load();
    void LoadForms();
    bool TapSprints();
};

namespace ModCompatibility {
    namespace TUDM {
        constexpr auto mod_name = "Ultimate Dodge Mod.esp";
        constexpr std::string_view script_name = "UDSKSEFunctionsScript";
        bool IsInstalled();
        inline bool is_installed = false;
        bool StopSneak();
        RE::BSScript::Internal::AttachedScript* GetScript();
        inline RE::TESQuest* quest_form = nullptr;
    };
};