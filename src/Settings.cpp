#include "Settings.h"

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
    if (const auto vm = skyrimVM ? skyrimVM->impl : nullptr) {
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