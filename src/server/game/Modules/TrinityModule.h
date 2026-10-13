/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#ifndef TRINITYCORE_TRINITYMODULE_H
#define TRINITYCORE_TRINITYMODULE_H
#include "Define.h"
#include "Log.h"
#include "StringFormat.h"
#include "TrinitySDKBuild.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
 #ifndef TRINITY_SDK_VERSION_MAJOR
#define TRINITY_SDK_VERSION_MAJOR 1
#define TRINITY_SDK_VERSION_MINOR 0
#define TRINITY_SDK_COMPILER "unknown"
#define TRINITY_SDK_HEADER_HASH "unknown"
#define TRINITY_SDK_CORE_REVISION "unknown"
#define TRINITY_SDK_CRT "unknown"
#define TRINITY_SDK_ARCH "unknown"
#endif
#define TRINITY_MODULE_STRINGIFY_IMPL(value) #value
#define TRINITY_MODULE_STRINGIFY(value) TRINITY_MODULE_STRINGIFY_IMPL(value)
#if defined(_LIBCPP_VERSION)
#define TRINITY_MODULE_STDLIB "libc++-" TRINITY_MODULE_STRINGIFY(_LIBCPP_VERSION)
#elif defined(__GLIBCXX__)
#define TRINITY_MODULE_STDLIB "libstdc++-" TRINITY_MODULE_STRINGIFY(__GLIBCXX__)
#elif defined(_MSVC_STL_VERSION)
#define TRINITY_MODULE_STDLIB "msvc-stl-" TRINITY_MODULE_STRINGIFY(_MSVC_STL_VERSION)
#else
#define TRINITY_MODULE_STDLIB "unknown"
#endif
#if defined(_ITERATOR_DEBUG_LEVEL)
#define TRINITY_MODULE_DEBUG "iterator-" TRINITY_MODULE_STRINGIFY(_ITERATOR_DEBUG_LEVEL)
#elif defined(_GLIBCXX_DEBUG)
#define TRINITY_MODULE_DEBUG "glibcxx-debug"
#elif defined(NDEBUG)
#define TRINITY_MODULE_DEBUG "release"
#else
#define TRINITY_MODULE_DEBUG "debug"
#endif
namespace Trinity
{
class ModuleContext;
class TC_GAME_API Module
{
public:
    virtual ~Module() = default;
    virtual void OnLoad(ModuleContext&) = 0;
    virtual void OnUnload() { }
    virtual void OnConfigReload() { }
};
using ModuleFactory = Module* (*)();
using ModuleDestroy = void (*)(Module*);
struct ModuleAbi
{
    uint32 SdkMajor;
    uint32 SdkMinor;
    char const* CompilerId;
    char const* HeaderSetHash;
    char const* CoreRevision;
    char const* Crt;
    char const* Architecture;
    char const* StandardLibrary;
    char const* DebugMode;
};
inline ModuleAbi const& GetModuleAbi()
{
    static ModuleAbi const abi{TRINITY_SDK_VERSION_MAJOR, TRINITY_SDK_VERSION_MINOR, TRINITY_SDK_COMPILER, TRINITY_SDK_HEADER_HASH, TRINITY_SDK_CORE_REVISION, TRINITY_SDK_CRT, TRINITY_SDK_ARCH, TRINITY_MODULE_STDLIB, TRINITY_MODULE_DEBUG};
    return abi;
}
class TC_GAME_API ModuleServices
{
public:
    template<typename Interface> void Provide(std::shared_ptr<Interface> service)
    {
        if (!service) throw std::invalid_argument("Module service cannot be null");
        if (!_services.emplace(std::type_index(typeid(Interface)), std::move(service)).second) throw std::logic_error("Module service already provided");
    }
    template<typename Interface> std::shared_ptr<Interface> Require() const
    {
        auto itr = _services.find(std::type_index(typeid(Interface)));
        if (itr == _services.end()) throw std::runtime_error("Required module service is unavailable");
        return std::static_pointer_cast<Interface>(itr->second);
    }
    void Clear() { _services.clear(); }
private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> _services;
};
class TC_GAME_API ModuleContext
{
public:
    class TC_GAME_API ConfigView
    {
    public:
        explicit ConfigView(std::string const& name) : _prefix("Module." + name + ".") { }
        std::string GetString(std::string_view key, std::string_view defaultValue = {}) const;
        int32 GetInt(std::string_view key, int32 defaultValue = 0) const;
        bool GetBool(std::string_view key, bool defaultValue = false) const;
    private: std::string _prefix;
    };
    class LogView
    {
    public:
        explicit LogView(std::string const& name) : _category("module." + name) { }
        template<typename... Args> void Info(Trinity::FormatString<Args...> format, Args&&... args) const { if (sLog->ShouldLog(_category, LOG_LEVEL_INFO)) sLog->OutMessage(_category, LOG_LEVEL_INFO, format, std::forward<Args>(args)...); }
        template<typename... Args> void Warn(Trinity::FormatString<Args...> format, Args&&... args) const { if (sLog->ShouldLog(_category, LOG_LEVEL_WARN)) sLog->OutMessage(_category, LOG_LEVEL_WARN, format, std::forward<Args>(args)...); }
        template<typename... Args> void Error(Trinity::FormatString<Args...> format, Args&&... args) const { if (sLog->ShouldLog(_category, LOG_LEVEL_ERROR)) sLog->OutMessage(_category, LOG_LEVEL_ERROR, format, std::forward<Args>(args)...); }
    private: std::string _category;
    };
    ModuleContext(std::string name, std::string manifestPath, ModuleServices& services) : _name(std::move(name)), _manifestPath(std::move(manifestPath)), _config(_name), _log(_name), _services(services) { }
    std::string const& GetName() const { return _name; }
    std::string const& GetManifestPath() const { return _manifestPath; }
    ConfigView const& Config() const { return _config; }
    LogView const& Log() const { return _log; }
    ModuleServices& Services() const { return _services; }
    template<typename Script, typename... Args> Script* AddScript(Args&&... args) { return new Script(std::forward<Args>(args)...); }
private:
    std::string _name;
    std::string _manifestPath;
    ConfigView _config;
    LogView _log;
    ModuleServices& _services;
};
}
#define TC_MODULE(TYPE, ...) \
    extern "C" TC_API_EXPORT Trinity::ModuleAbi const* TrinityModuleAbi() { return &Trinity::GetModuleAbi(); } \
    extern "C" TC_API_EXPORT char const* TrinityModuleManifest() { return TRINITY_MODULE_MANIFEST; } \
    extern "C" TC_API_EXPORT Trinity::Module* TrinityModuleCreate() { return new TYPE(); } \
    extern "C" TC_API_EXPORT void TrinityModuleDestroy(Trinity::Module* module) { delete module; }
#endif
