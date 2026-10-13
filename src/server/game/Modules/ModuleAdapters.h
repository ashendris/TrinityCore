/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#ifndef TRINITYCORE_MODULEADAPTERS_H
#define TRINITYCORE_MODULEADAPTERS_H

#include "TrinityModule.h"
#include <cstdint>
#include <string>
namespace Trinity
{
// Called by ModuleMgr after a module has successfully loaded a callback table.
// The table is copied into host-owned storage; the caller may release its copy.
bool RegisterAbiFamily(trinity_host const* host, uint32_t family, void const* callbacks, std::string& error);
// Revokes every callback scope and destroys all host-owned table storage. This
// MUST run before the module's unload entry point and before the library closes.
void InvalidateAbiHost(trinity_host const* host);
// Capability implementation installed by ModuleMgr's host query.
trinity_object_api_v1 const* GetAbiObjectApi();
bool RegisterConcreteScriptFamily(trinity_host const* host, uint32_t family, void const* callbacks, std::string& error);
void InvalidateConcreteScriptHost(trinity_host const* host);

// Adapter-side scope helpers. A callback wrapper begins a scope before passing
// operands to module code and ends it immediately after return. Handles are
// generation-tagged and are never addresses.
trinity_callback_scope BeginAbiCallbackScope(trinity_host const* host, uint32_t family);
void EndAbiCallbackScope(trinity_host const* host, trinity_callback_scope& scope);
trinity_handle RegisterAbiObject(trinity_host const* host, trinity_callback_scope const& scope,
    trinity_object_kind kind, void* object);
}
#endif
