// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
namespace DevBenchAPI {
constexpr const auto DevBenchPluginName = "devbench"; using WriteFn = void (*)(void*, const char*); using ToolFn = void (*)(void*, const char*, void*, WriteFn);
struct DevBenchMessage { enum : std::uint32_t { kMessage_GetInterface = 0x9a3f1c08 }; void* (*GetApiFunction)(unsigned int) = nullptr; };
struct IDevBenchInterface001; IDevBenchInterface001* GetDevBenchInterface001();
struct IDevBenchInterface001 {
virtual unsigned int GetBuildNumber() = 0;
virtual bool RegisterTool(const char*, const char*, ToolFn, void*) = 0;
virtual void EmitEvent(const char*, const char*) = 0;
virtual bool RegisterMenuHandler(const char*, const char*, ToolFn, void*) = 0;
virtual bool RegisterToolExtension(const char*, const char*, const char*, ToolFn, void*) = 0;
};
}
extern DevBenchAPI::IDevBenchInterface001* g_devBenchInterface;
