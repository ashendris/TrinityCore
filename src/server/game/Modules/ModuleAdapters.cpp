#include "ModuleAdapters.h"
#include <mutex>
#include <unordered_map>
#include <vector>
namespace Trinity
{
namespace
{
struct ObjectEntry { trinity_object_kind kind; void* value; uint64_t scope; };
struct HostState
{
    std::mutex mutex;
    uint64_t nextToken = 1;
    uint64_t nextHandle = 1;
    std::unordered_map<uint64_t, std::unordered_map<trinity_handle, ObjectEntry>> scopes;
    std::unordered_map<uint32_t, std::vector<uint8_t>> families;
    bool valid = true;
};
std::mutex StatesMutex;
std::unordered_map<trinity_host const*, HostState*> States;
HostState* State(trinity_host const* host)
{
    std::lock_guard lock(StatesMutex);
    auto itr = States.find(host);
    return itr == States.end() ? nullptr : itr->second;
}
int32_t Begin(trinity_host const* host, uint32_t family, trinity_callback_scope* out)
{
    if (!out) return -1;
    HostState* state = State(host);
    if (!state) return -2;
    std::lock_guard lock(state->mutex);
    if (!state->valid) return -3;
    uint64_t token = state->nextToken++;
    state->scopes.emplace(token, std::unordered_map<trinity_handle, ObjectEntry>{});
    out->token = token; out->family = family; out->reserved = 0;
    return 0;
}
void End(trinity_host const* host, trinity_callback_scope* scope)
{
    HostState* state = State(host);
    if (!state || !scope) return;
    std::lock_guard lock(state->mutex);
    auto itr = state->scopes.find(scope->token);
    if (itr == state->scopes.end() || scope->family == 0) return;
    state->scopes.erase(itr);
    scope->token = 0; scope->family = 0; scope->reserved = 0;
}
int32_t Query(trinity_host const* host, trinity_callback_scope const* scope, trinity_handle handle,
    trinity_object_kind kind, uint32_t field, void* out, size_t* size)
{
    if (!out || !size) return -1;
    HostState* state = State(host);
    if (!state || !scope || !scope->token || !scope->family) return -2;
    std::lock_guard lock(state->mutex);
    auto sit = state->scopes.find(scope->token);
    if (sit == state->scopes.end()) return -3;
    auto oit = sit->second.find(handle);
    if (oit == sit->second.end() || oit->second.kind != kind || !oit->second.value) return -4;
    (void)field;
    return -6;
}
int32_t Mutate(trinity_host const* host, trinity_callback_scope const* scope, trinity_handle handle,
    trinity_object_kind kind, uint32_t field, void const* value, size_t size)
{
    (void)host; (void)scope; (void)handle; (void)kind; (void)field; (void)value; (void)size;
    return -6;
}
int32_t Resize(trinity_host const*, trinity_buffer* buffer, size_t size)
{
    if (!buffer || (buffer->flags & TRINITY_BUFFER_READONLY) || size > buffer->capacity) return -1;
    buffer->size = size;
    return 0;
}
trinity_object_api_v1 ObjectApi{
    sizeof(trinity_object_api_v1), TRINITY_ABI_V1, Begin, End, Query, Mutate, Resize
};
}

bool RegisterAbiFamily(trinity_host const* host, uint32_t family, void const* callbacks, std::string& error)
{
    (void)host; (void)family; (void)callbacks;
    error = "no concrete ABI bridge is registered for this family";
    return false;
}
void InvalidateAbiHost(trinity_host const* host)
{
    std::lock_guard lock(StatesMutex);
    auto itr = States.find(host);
    if (itr == States.end()) return;
    { std::lock_guard stateLock(itr->second->mutex); itr->second->valid = false; itr->second->scopes.clear(); itr->second->families.clear(); }
    delete itr->second;
    States.erase(itr);
}
trinity_object_api_v1 const* GetAbiObjectApi() { return &ObjectApi; }
trinity_callback_scope BeginAbiCallbackScope(trinity_host const* host, uint32_t family)
{
    trinity_callback_scope scope{}; Begin(host, family, &scope); return scope;
}
void EndAbiCallbackScope(trinity_host const* host, trinity_callback_scope& scope) { End(host, &scope); scope = {}; }
trinity_handle RegisterAbiObject(trinity_host const* host, trinity_callback_scope const& scope, trinity_object_kind kind, void* object)
{
    HostState* state = State(host);
    if (!state || !object) return 0;
    std::lock_guard lock(state->mutex);
    auto itr = state->scopes.find(scope.token);
    if (itr == state->scopes.end()) return 0;
    trinity_handle handle = state->nextHandle++;
    itr->second.emplace(handle, ObjectEntry{kind, object, scope.token});
    return handle;
}
}
