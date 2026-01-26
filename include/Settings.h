#pragma once

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