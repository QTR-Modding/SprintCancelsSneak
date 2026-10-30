#include "Settings.h"

void Settings::Load() {
    constexpr auto path = "Data/SKSE/Plugins/SprintCancelsSneak.ini";
    std::ifstream file(path);
    if (!file) {
        logger::info("{} not found, iTapAction = {}", path, tap_action);
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (const auto comment = line.find(';'); comment != std::string::npos) {
            line.erase(comment);
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        std::erase_if(key, [](unsigned char c) { return std::isspace(c); });
        if (_stricmp(key.c_str(), "iTapAction") != 0) continue;
        try {
            const auto value = std::stoi(line.substr(eq + 1));
            if (value >= 0 && value <= 2) tap_action = value;
        } catch (...) {
            logger::error("iTapAction is not a number: {}", line);
        }
    }
    logger::info("iTapAction = {}", tap_action);
}

void Settings::LoadForms() {
    roll_perk = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSPerk>(roll_perk_id, "Skyrim.esm");
    if (!roll_perk) logger::error("Sneak roll perk {:X} not found", roll_perk_id);
}

bool Settings::TapSprints() {
    switch (tap_action) {
        case 1:
            return true;
        case 2: {
            const auto player = RE::PlayerCharacter::GetSingleton();
            return !(roll_perk && player && player->HasPerk(roll_perk));
        }
        default:
            return false;
    }
}

bool ModCompatibility::TUDM::IsInstalled() {
    if (RE::TESDataHandler::GetSingleton()->LookupLoadedModByName(mod_name)) {
        logger::info("Ultimate Dodge Mod detected");
        quest_form = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x000D27A1, mod_name);
        if (quest_form) return true;
        logger::error("Ultimate Dodge Mod quest form not found");
    }
    return false;
}

bool ModCompatibility::TUDM::StopSneak() {
    const auto skyrimVM = RE::SkyrimVM::GetSingleton();
    if (const auto vm = skyrimVM ? skyrimVM->GetVM() : nullptr) {
        if (const auto script = GetScript()) {
            auto obj = static_cast<RE::BSTSmartPointer<RE::BSScript::Object>>(script->get());
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            const auto vmargs = RE::MakeFunctionArguments();
            vm->DispatchMethodCall(obj, "EndSneakMode", vmargs, callback);
            delete vmargs;
        }
    }
    return false;
}

RE::BSScript::Internal::AttachedScript* ModCompatibility::TUDM::GetScript() {
    if (const auto virtualMachine = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
        const auto handlePolicy = virtualMachine->GetObjectHandlePolicy();
        if (const auto bindPolicy = virtualMachine->GetObjectBindPolicy(); handlePolicy && bindPolicy) {
            if (const auto newHandler = handlePolicy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest_form);
                newHandler != handlePolicy->EmptyHandle()) {
                const auto vm_scripts_hashmap = &virtualMachine->attachedScripts;
                if (const auto newHandlerScripts_it = vm_scripts_hashmap->find(newHandler);
                    newHandlerScripts_it != vm_scripts_hashmap->end()) {
                    for (auto& script : newHandlerScripts_it->second) {
                        if (const auto oti = script->GetTypeInfo()) {
                            if (oti->name == script_name) {
                                return &script;
                            }
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}
