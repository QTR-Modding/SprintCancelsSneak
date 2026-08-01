// SPDX-License-Identifier: MIT
#include "DevBenchAPI.h"
DevBenchAPI::IDevBenchInterface001* g_devBenchInterface = nullptr;
namespace DevBenchAPI {
IDevBenchInterface001* GetDevBenchInterface001() {
if (g_devBenchInterface) return g_devBenchInterface;
const auto messaging = SKSE::GetMessagingInterface(); if (!messaging) return nullptr;
DevBenchMessage message;
messaging->Dispatch(DevBenchMessage::kMessage_GetInterface, &message, sizeof(DevBenchMessage*), DevBenchPluginName);
if (!message.GetApiFunction) return nullptr;
g_devBenchInterface = static_cast<IDevBenchInterface001*>(message.GetApiFunction(1));
return g_devBenchInterface;
}}
