/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#include "ModuleMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include <algorithm>
#include <functional>
#include <fstream>
#include <iterator>
#include <set>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <rapidjson/document.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace Trinity
{
TC_GAME_API std::string ModuleContext::ConfigView::GetString(std::string_view key, std::string_view def) const { return sConfigMgr->GetStringDefault(Trinity::StringFormat("{}{}", _prefix, key), def); }
TC_GAME_API int32 ModuleContext::ConfigView::GetInt(std::string_view key, int32 def) const { return sConfigMgr->GetIntDefault(Trinity::StringFormat("{}{}", _prefix, key), def); }
TC_GAME_API bool ModuleContext::ConfigView::GetBool(std::string_view key, bool def) const { return sConfigMgr->GetBoolDefault(Trinity::StringFormat("{}{}", _prefix, key), def); }
namespace
{
struct Manifest { std::string name, version, sdk, path; std::vector<std::string> depends; int loadOrder = 0; };
struct Handle { 
#if defined(_WIN32)
    HMODULE value = nullptr;
#else
    void* value = nullptr;
#endif
    ~Handle()
    {
#if defined(_WIN32)
        if (value) FreeLibrary(value);
#else
        if (value) dlclose(value);
#endif
    }
    Handle() = default;
    Handle(Handle const&) = delete;
    Handle& operator=(Handle const&) = delete;
};
using AbiFn = ModuleAbi const* (*)();
using ManifestFn = char const* (*)();
static std::string ReadFile(boost::filesystem::path const& path) { std::ifstream stream(path.string()); return {std::istreambuf_iterator<char>(stream), {}}; }
static bool ParseManifest(boost::filesystem::path const& path, Manifest& out)
{
    rapidjson::Document doc; std::string text = ReadFile(path); if (text.empty() || doc.Parse(text.c_str()).HasParseError() || !doc.IsObject()) return false;
    auto string = [&doc](char const* key) { auto itr = doc.FindMember(key); return itr != doc.MemberEnd() && itr->value.IsString() ? std::string(itr->value.GetString()) : std::string{}; };
    out.name = string("name"); out.version = string("version"); out.sdk = string("sdk"); out.path = path.string();
    if (auto itr = doc.FindMember("load_order"); itr != doc.MemberEnd() && itr->value.IsInt()) out.loadOrder = itr->value.GetInt();
    if (auto itr = doc.FindMember("depends"); itr != doc.MemberEnd() && itr->value.IsArray()) for (auto const& dep : itr->value.GetArray()) if (dep.IsString()) out.depends.emplace_back(dep.GetString());
    return !out.name.empty() && !out.version.empty();
}
static std::string DependencyName(std::string value) { auto pos = value.find_first_of(" <>=!"); return value.substr(0, pos); }
static bool Same(char const* a, char const* b) { return a && b && std::string_view(a) == b; }
}
struct ModuleMgr::Impl
{
    struct Loaded { Manifest manifest; std::unique_ptr<Handle> handle; ModuleDestroy destroy; std::unique_ptr<ModuleContext> context; Module* instance; };
    std::vector<Manifest> discovered; std::vector<Loaded> loaded; ModuleServices services; boost::filesystem::path root; bool discoveredDone = false; bool loadedDone = false;
};
ModuleMgr::ModuleMgr() : _impl(std::make_unique<Impl>()) { }
ModuleMgr::~ModuleMgr() = default;
ModuleMgr* ModuleMgr::instance() { static ModuleMgr manager; return &manager; }
void ModuleMgr::Discover(std::string const& moduleDir)
{
    if (_impl->discoveredDone) return; _impl->root = boost::filesystem::absolute(moduleDir.empty() ? "modules" : moduleDir); _impl->discoveredDone = true;
    if (!boost::filesystem::is_directory(_impl->root)) return;
    for (auto const& entry : boost::filesystem::directory_iterator(_impl->root)) { if (!boost::filesystem::is_directory(entry.path())) continue; auto manifest = entry.path() / "module.json"; Manifest value; if (ParseManifest(manifest, value)) _impl->discovered.push_back(std::move(value)); }
    std::stable_sort(_impl->discovered.begin(), _impl->discovered.end(), [](Manifest const& a, Manifest const& b) { return a.loadOrder != b.loadOrder ? a.loadOrder < b.loadOrder : a.name < b.name; });
    TC_LOG_INFO("server.modules", "Discovered {} module manifest(s)", _impl->discovered.size());
}
bool ModuleMgr::Load()
{
    if (_impl->loadedDone) return true; Discover();
    std::unordered_map<std::string, size_t> indices; for (size_t i = 0; i < _impl->discovered.size(); ++i) indices.emplace(_impl->discovered[i].name, i);
    std::vector<size_t> order; std::set<size_t> visiting, visited;
    std::function<bool(size_t)> visit = [&](size_t i) { if (visited.contains(i)) return true; if (!visiting.insert(i).second) return false; for (std::string const& dep : _impl->discovered[i].depends) { auto itr = indices.find(DependencyName(dep)); if (itr == indices.end() || !visit(itr->second)) return false; } visiting.erase(i); visited.insert(i); order.push_back(i); return true; };
    try {
        for (size_t i = 0; i < _impl->discovered.size(); ++i) if (!visit(i)) throw std::runtime_error("module dependency cycle or missing dependency");
        for (size_t i : order) {
            Manifest const& manifest = _impl->discovered[i]; boost::filesystem::path dir = boost::filesystem::path(manifest.path).parent_path();
#if defined(_WIN32)
            auto library = dir / (manifest.name + ".dll"); HMODULE raw = LoadLibraryW(library.wstring().c_str()); if (!raw) throw std::runtime_error("unable to load module library " + library.string());
#else
            auto library = dir / (manifest.name + ".so"); void* raw = dlopen(library.string().c_str(), RTLD_NOW | RTLD_LOCAL); if (!raw) throw std::runtime_error("unable to load module library " + library.string());
#endif
            auto handle = std::make_unique<Handle>(); handle->value = raw;
#if defined(_WIN32)
#define MODULE_SYMBOL(name) GetProcAddress(raw, name)
#else
#define MODULE_SYMBOL(name) dlsym(raw, name)
#endif
            auto abi = reinterpret_cast<AbiFn>(MODULE_SYMBOL("TrinityModuleAbi")); auto sidecar = reinterpret_cast<ManifestFn>(MODULE_SYMBOL("TrinityModuleManifest")); auto factory = reinterpret_cast<ModuleFactory>(MODULE_SYMBOL("TrinityModuleCreate")); auto destroy = reinterpret_cast<ModuleDestroy>(MODULE_SYMBOL("TrinityModuleDestroy"));
            if (!abi || !sidecar || !factory || !destroy) throw std::runtime_error("module exports are incomplete: " + manifest.name);
            ModuleAbi const* value = abi(); ModuleAbi const& expected = GetModuleAbi(); if (!value || value->SdkMajor != expected.SdkMajor || value->SdkMinor != expected.SdkMinor || !Same(value->CompilerId, expected.CompilerId) || !Same(value->HeaderSetHash, expected.HeaderSetHash) || !Same(value->CoreRevision, expected.CoreRevision) || !Same(value->Crt, expected.Crt) || !Same(value->Architecture, expected.Architecture) || !Same(value->StandardLibrary, expected.StandardLibrary) || !Same(value->DebugMode, expected.DebugMode)) throw std::runtime_error("module ABI mismatch: " + manifest.name);
            if (char const* embedded = sidecar(); embedded && std::string(embedded) != ReadFile(manifest.path)) throw std::runtime_error("module manifest mismatch: " + manifest.name);
            Module* instance = factory(); if (!instance) throw std::runtime_error("module factory returned null: " + manifest.name);
            std::string contextName = "___module_" + manifest.name + "___"; sScriptMgr->SetScriptContext(contextName); auto context = std::make_unique<ModuleContext>(manifest.name, manifest.path, _impl->services); instance->OnLoad(*context); _impl->loaded.push_back({manifest, std::move(handle), destroy, std::move(context), instance});
            TC_LOG_INFO("server.modules", "Loaded module {} v{}", manifest.name, manifest.version);
#undef MODULE_SYMBOL
        }
        sScriptMgr->SwapScriptContext(); _impl->loadedDone = true; return true;
    } catch (...) { Unload(); throw; }
}
void ModuleMgr::Unload()
{
    for (auto itr = _impl->loaded.rbegin(); itr != _impl->loaded.rend(); ++itr) { sScriptMgr->ReleaseScriptContext("___module_" + itr->manifest.name + "___"); try { itr->instance->OnUnload(); } catch (...) { TC_LOG_ERROR("server.modules", "Module unload failed; continuing shutdown"); } itr->destroy(itr->instance); }
    if (!_impl->loaded.empty()) sScriptMgr->SwapScriptContext(); _impl->loaded.clear(); _impl->services.Clear(); _impl->loadedDone = false;
}
void ModuleMgr::OnConfigReload() { for (auto& module : _impl->loaded) module.instance->OnConfigReload(); }
bool ModuleMgr::RegisterDatabaseUpdates(std::string const&)
{
    for (auto const& module : _impl->discovered) { auto path = boost::filesystem::path(module.path).parent_path() / "sql" / "world"; if (!boost::filesystem::is_directory(path)) continue; std::string escaped = boost::filesystem::absolute(path).generic_string(); WorldDatabase.EscapeString(escaped); WorldDatabase.DirectPExecute("INSERT IGNORE INTO updates_include (path, state) VALUES ('{}', 'RELEASED')", escaped); } return true;
}
std::vector<std::string> ModuleMgr::GetLoadedModuleInfo() const { std::vector<std::string> result; for (auto const& module : _impl->loaded) result.push_back(Trinity::StringFormat("{} v{}", module.manifest.name, module.manifest.version)); return result; }
std::string ModuleMgr::GetModuleInfo(std::string const& name) const { for (auto const& module : _impl->loaded) if (module.manifest.name == name) return Trinity::StringFormat("{} v{}", module.manifest.name, module.manifest.version); return {}; }
bool ModuleMgr::OwnsContext(std::string const& context) const { return context.starts_with("___module_"); }
}
