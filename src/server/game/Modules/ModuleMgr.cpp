/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#include "ModuleMgr.h"
#include "Chat.h"
#include "ChatCommands/ChatCommand.h"
#include "Config.h"
#include "CryptoHash.h"
#include "DatabaseEnv.h"
#include "ScriptMgr.h"
#include "ModuleSpellAdapters.h"
#include "ModuleAdapters.h"
#include "StringFormat.h"
#include "ed25519/ed25519.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <cctype>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <vector>
#include <boost/filesystem.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#if defined(_WIN32)
#include <windows.h>
#undef GetObject
#else
#include <dlfcn.h>
#endif
#include <stdexcept>

struct trinity_module { void* owner = nullptr; };
struct trinity_host { trinity_host_api_v1 api{}; void* owner = nullptr; };
struct trinity_command_context { ChatHandler* handler = nullptr; };

namespace Trinity
{
namespace
{
struct Manifest
{
    std::string name, version, sdk, path, keyId, signature, librarySha256, canonical;
    std::vector<std::string> depends;
    std::vector<std::string> databases;
    std::map<std::string, std::string> files;
    int loadOrder = 0;
};
struct Handle
{
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
static std::string ReadFile(boost::filesystem::path const& path)
{
    std::ifstream stream(path.string(), std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
static bool DecodeHex(std::string_view input, uint8_t* output, size_t size)
{
    if (input.size() != size * 2)
        return false;
    auto hex = [](char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    for (size_t i = 0; i < size; ++i)
    {
        int hi = hex(input[i * 2]), lo = hex(input[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        output[i] = uint8_t((hi << 4) | lo);
    }
    return true;
}
static bool SafeBasename(std::string_view value)
{
    if (value.empty() || value == "." || value == ".." || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string_view::npos)
        return false;
    return value.find('/') == std::string_view::npos && value.find('\\') == std::string_view::npos;
}
static bool SafeRelativePath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.back() == '/') return false;
    size_t begin = 0;
    while (begin < path.size())
    {
        size_t end = path.find('/', begin);
        if (end == std::string_view::npos) end = path.size();
        if (!SafeBasename(path.substr(begin, end - begin))) return false;
        begin = end + 1;
    }
    return true;
}
static bool CanonicalJson(rapidjson::Value const& value, rapidjson::Writer<rapidjson::StringBuffer>& writer)
{
    if (value.IsObject())
    {
        std::map<std::string, rapidjson::Value const*> members;
        for (auto const& member : value.GetObject())
            if (!members.emplace(std::string(member.name.GetString(), member.name.GetStringLength()), &member.value).second) return false;
        writer.StartObject();
        for (auto const& [key, child] : members)
        {
            writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
            if (!CanonicalJson(*child, writer)) return false;
        }
        return writer.EndObject();
    }
    if (value.IsArray())
    {
        writer.StartArray();
        for (auto const& child : value.GetArray()) if (!CanonicalJson(child, writer)) return false;
        return writer.EndArray();
    }
    if (value.IsString()) return writer.String(value.GetString(), value.GetStringLength());
    if (value.IsInt()) return writer.Int(value.GetInt());
    return false;
}
static bool ValidVersion(std::string_view version);
static bool ParseManifest(boost::filesystem::path const& path, Manifest& out)
{
    rapidjson::Document doc;
    std::string text = ReadFile(path);
    if (text.empty() || doc.Parse<rapidjson::kParseValidateEncodingFlag>(text.data(), text.size()).HasParseError() || !doc.IsObject())
        return false;
    std::unordered_set<std::string> names;
    for (auto const& member : doc.GetObject())
        if (!names.emplace(member.name.GetString(), member.name.GetStringLength()).second)
            return false;
    auto string = [&doc](char const* key)
    {
        auto itr = doc.FindMember(key);
        return itr != doc.MemberEnd() && itr->value.IsString() && std::strlen(itr->value.GetString()) == itr->value.GetStringLength() ? std::string(itr->value.GetString(), itr->value.GetStringLength()) : std::string{};
    };
    for (auto const& member : doc.GetObject())
    {
        std::string_view key(member.name.GetString(), member.name.GetStringLength());
        if (key != "name" && key != "version" && key != "sdk" && key != "depends" && key != "load_order" && key != "databases" && key != "key_id" && key != "signature" && key != "library_sha256" && key != "files")
            return false;
    }
    out.name = string("name"); out.version = string("version"); out.sdk = string("sdk");
    out.keyId = string("key_id"); out.signature = string("signature"); out.librarySha256 = string("library_sha256"); out.path = path.string();
    if (!SafeBasename(out.name) || !SafeBasename(out.keyId) || !ValidVersion(out.version) || out.sdk != ">=1.0 <2.0" || out.signature.empty() || out.librarySha256.empty())
        return false;
    if (auto itr = doc.FindMember("load_order"); itr != doc.MemberEnd())
    {
        if (!itr->value.IsInt()) return false;
        out.loadOrder = itr->value.GetInt();
    }
    if (auto itr = doc.FindMember("depends"); itr != doc.MemberEnd())
    {
        if (!itr->value.IsArray()) return false;
        for (auto const& dependency : itr->value.GetArray())
            if (!dependency.IsString() || dependency.GetStringLength() == 0) return false;
            else out.depends.emplace_back(dependency.GetString());
    }
    if (auto itr = doc.FindMember("databases"); itr != doc.MemberEnd())
    {
        if (!itr->value.IsArray()) return false;
        for (auto const& database : itr->value.GetArray())
            if (!database.IsString() || !SafeBasename(database.GetString())) return false;
            else out.databases.emplace_back(database.GetString());
    }
    auto files = doc.FindMember("files");
    if (files == doc.MemberEnd() || !files->value.IsObject() || files->value.MemberCount() == 0)
        return false;
    for (auto const& file : files->value.GetObject())
    {
        std::string relative(file.name.GetString(), file.name.GetStringLength());
        boost::filesystem::path relativePath(relative);
        if (!SafeRelativePath(relative) || relative == "module.json" || !file.value.IsString() || file.value.GetStringLength() != 64)
            return false;
        std::array<uint8_t, 32> digest{};
        if (!DecodeHex(std::string_view(file.value.GetString(), file.value.GetStringLength()), digest.data(), digest.size()))
            return false;
        if (!out.files.emplace(std::move(relative), std::string(file.value.GetString(), file.value.GetStringLength())).second) return false;
    }
    doc.RemoveMember("signature");
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!CanonicalJson(doc, writer)) return false;
    out.canonical.assign(buffer.GetString(), buffer.GetSize());
    return true;
}
static bool VerifyPackage(Manifest const& manifest, boost::filesystem::path const& library)
{
    boost::filesystem::path packageRoot = boost::filesystem::path(manifest.path).parent_path();
    boost::filesystem::path keyDirectory = boost::filesystem::path(sConfigMgr->GetStringDefault("Modules.TrustedKeyDirectory", "keys"));
    boost::filesystem::path keyPath = keyDirectory / (manifest.keyId + ".pub");
    if (!SafeBasename(manifest.keyId) || !boost::filesystem::is_regular_file(keyPath) || boost::filesystem::is_symlink(keyPath))
        return false;
    std::string key = ReadFile(keyPath);
    std::array<uint8_t, 32> publicKey{};
    std::array<uint8_t, 64> signature{};
    if (key.size() == publicKey.size()) std::copy(key.begin(), key.end(), publicKey.begin());
    else if (!DecodeHex(key, publicKey.data(), publicKey.size()))
        return false;
    if (!DecodeHex(manifest.signature, signature.data(), signature.size()))
        return false;
    std::set<std::string> actual;
    for (boost::filesystem::recursive_directory_iterator itr(packageRoot), end; itr != end; ++itr)
    {
        if (boost::filesystem::is_symlink(itr->path())) return false;
        if (!boost::filesystem::is_regular_file(itr->path())) continue;
        std::string relative = boost::filesystem::relative(itr->path(), packageRoot).generic_string();
        if (relative == "module.json") continue;
        actual.insert(relative);
        std::string bytes = ReadFile(itr->path());
        auto digest = Crypto::SHA256::GetDigestOf(reinterpret_cast<uint8 const*>(bytes.data()), bytes.size());
        std::array<uint8_t, 32> expected{};
        auto expectedItr = manifest.files.find(relative);
        if (expectedItr == manifest.files.end() || !DecodeHex(expectedItr->second, expected.data(), expected.size()) || digest != expected)
            return false;
    }
    if (actual.size() != manifest.files.size()) return false;
    auto libraryRelative = boost::filesystem::relative(library, packageRoot).generic_string();
    if (!manifest.files.contains(libraryRelative)) return false;
    return ED25519_verify(reinterpret_cast<uint8_t const*>(manifest.canonical.data()), manifest.canonical.size(), signature.data(), publicKey.data()) == 1;
}
static std::string DependencyName(std::string const& dependency)
{
    size_t start = dependency.find_first_not_of(" \t");
    size_t end = dependency.find_first_of(" \t>=@<");
    if (start == std::string::npos)
        return {};
    return dependency.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
static bool ValidVersion(std::string_view version)
{
    return !version.empty() && version.find_first_not_of("0123456789.") == std::string_view::npos && version.front() != '.' && version.back() != '.' && version.find("..") == std::string_view::npos;
}
static int CompareVersion(std::string_view left, std::string_view right)
{
    if (!ValidVersion(left) || !ValidVersion(right)) return 0;
    size_t lpos = 0, rpos = 0;
    while (lpos < left.size() || rpos < right.size())
    {
        auto part = [](std::string_view value, size_t& position)
        {
            size_t begin = position;
            while (position < value.size() && value[position] != '.') ++position;
            int result = 0;
            for (size_t i = begin; i < position; ++i) result = result * 10 + value[i] - '0';
            if (position < value.size()) ++position;
            return result;
        };
        int lpart = part(left, lpos), rpart = part(right, rpos);
        if (lpart != rpart) return lpart < rpart ? -1 : 1;
    }
    return 0;
}
static bool DependencySatisfied(std::string const& dependency, Manifest const& provider)
{
    if (DependencyName(dependency) != provider.name) return false;
    size_t position = dependency.find(">=");
    if (position != std::string::npos) return ValidVersion(dependency.substr(position + 2)) && CompareVersion(provider.version, dependency.substr(position + 2)) >= 0;
    position = dependency.find('=');
    return position == std::string::npos ? true : ValidVersion(dependency.substr(position + 1)) && CompareVersion(provider.version, dependency.substr(position + 1)) == 0;
}
struct ModuleOwner;
struct ServiceEntry { void* value = nullptr; ModuleOwner* owner = nullptr; };
struct ModuleRegistry { std::unordered_map<uint32_t, ServiceEntry> services; };
struct Registration
{
    trinity_service_registration service{};
    std::string serviceName;
    trinity_command_registration command{};
    std::string commandName;
    std::string help;
    struct Handler
    {
        Registration* registration = nullptr;
        bool operator()(ChatHandler* handler, char const* arguments) const
        {
            if (!registration || !registration->command.execute)
                return false;
            trinity_command_context context{handler};
            return registration->command.execute(registration->command.context, &context, arguments ? arguments : "", arguments ? std::strlen(arguments) : 0) == 0;
        }
    } handler;
};
struct ModuleOwner
{
    trinity_module module;
    trinity_host host;
    ModuleRegistry* registry = nullptr;
    std::string contextName;
    void* moduleContext = nullptr;
    trinity_module_api api{};
    std::vector<Registration> registrations;
    trinity_host_query query{};
    trinity_log_api log{};
    trinity_host_api_v1 hostApi{};
    std::optional<trinity_world_callbacks> worldCallbacks;
    class ModuleWorldScript* worldScript = nullptr;
    class ModuleCommandScript* commandScript = nullptr;
    mutable std::vector<Trinity::ChatCommands::ChatCommandBuilder> commandBuilders;
};
static bool InvokeModuleCommand(void* context, ChatHandler* handler, std::string_view arguments) noexcept
{
    auto* registration = static_cast<Registration*>(context);
    if (!registration || !registration->command.execute)
        return false;
    trinity_command_context commandContext{ handler };
    return registration->command.execute(registration->command.context, &commandContext, arguments.data(), arguments.size()) == 0;
}
static ModuleOwner* Owner(trinity_module* module) { return module ? static_cast<ModuleOwner*>(module->owner) : nullptr; }
static ModuleOwner* Owner(trinity_host const* host) { return host ? static_cast<ModuleOwner*>(host->owner) : nullptr; }
static ModuleOwner* Owner(trinity_host* host) { return host ? static_cast<ModuleOwner*>(host->owner) : nullptr; }
static void HostLog(trinity_host*, uint32_t level, char const* message, size_t length)
{
    std::string text(message ? message : "", length);
    if (level >= 3) TC_LOG_ERROR("server.modules", "{}", text); else if (level == 2) TC_LOG_WARN("server.modules", "{}", text); else TC_LOG_INFO("server.modules", "{}", text);
}
static int32_t GetConfigString(trinity_host*, char const* key, char const* fallback, char* output, size_t capacity)
{
    if (!key || (!output && capacity))
        return -1;
    std::string value = sConfigMgr->GetStringDefault(key, fallback ? fallback : "");
    size_t required = value.size() + 1;
    if (!output || capacity < required)
    {
        if (output && capacity)
        {
            std::memcpy(output, value.data(), capacity - 1);
            output[capacity - 1] = '\0';
        }
        return static_cast<int32_t>(required);
    }
    std::memcpy(output, value.c_str(), required);
    return static_cast<int32_t>(value.size());
}
static void SendSysMessage(trinity_command_context* context, char const* message, size_t length)
{
    if (context && context->handler)
        context->handler->SendSysMessage(std::string_view(message ? message : "", length));
}
static uint8_t IsConsole(trinity_command_context* context) { return context && context->handler && context->handler->IsConsole(); }
class ModuleWorldScript final : public WorldScript
{
public:
    ModuleWorldScript(ModuleOwner* owner, trinity_world_callbacks callbacks) : WorldScript(owner->contextName.c_str()), _owner(owner), _callbacks(callbacks) { }
    void OnOpenStateChange(bool open) override { if (_callbacks.OnOpenStateChange) _callbacks.OnOpenStateChange(_callbacks.context, &_owner->host, open); }
    void OnConfigLoad(bool reload) override { if (_callbacks.OnConfigLoad) _callbacks.OnConfigLoad(_callbacks.context, &_owner->host, reload); }
    void OnMotdChange(std::string& motd) override { if (_callbacks.OnMotdChange) _callbacks.OnMotdChange(_callbacks.context, &_owner->host, reinterpret_cast<trinity_handle>(&motd)); }
    void OnShutdownInitiate(ShutdownExitCode code, ShutdownMask mask) override { if (_callbacks.OnShutdownInitiate) _callbacks.OnShutdownInitiate(_callbacks.context, &_owner->host, static_cast<int32_t>(code), static_cast<int32_t>(mask)); }
    void OnShutdownCancel() override { if (_callbacks.OnShutdownCancel) _callbacks.OnShutdownCancel(_callbacks.context, &_owner->host); }
    void OnUpdate(uint32 diff) override { if (_callbacks.OnUpdate) _callbacks.OnUpdate(_callbacks.context, &_owner->host, diff); }
    void OnStartup() override { if (_callbacks.OnStartup) _callbacks.OnStartup(_callbacks.context, &_owner->host); }
    void OnShutdown() override { if (_callbacks.OnShutdown) _callbacks.OnShutdown(_callbacks.context, &_owner->host); }
private:
    ModuleOwner* _owner;
    trinity_world_callbacks _callbacks;
};
class ModuleCommandScript final : public CommandScript
{
public:
    explicit ModuleCommandScript(ModuleOwner* owner) : CommandScript(owner->contextName.c_str()), _owner(owner) { }
    std::span<Trinity::ChatCommands::ChatCommandBuilder const> GetCommands() const override
    {
        if (_owner->commandBuilders.empty())
            for (Registration& registration : _owner->registrations)
                if (registration.command.execute)
                    _owner->commandBuilders.emplace_back(registration.commandName, &InvokeModuleCommand, &registration, static_cast<rbac::RBACPermissions>(registration.command.permission), registration.command.allow_console ? Trinity::ChatCommands::Console::Yes : Trinity::ChatCommands::Console::No);
        return _owner->commandBuilders;
    }
private:
    ModuleOwner* _owner;
};
static void* HostQuery(trinity_host* host, uint32_t id) { ModuleOwner* owner = Owner(host); return owner && id == TRINITY_ABI_V1 ? &owner->hostApi : nullptr; }
static int32_t RegisterService(trinity_host* host, trinity_service_registration const* registration)
{
    ModuleOwner* owner = Owner(host);
    if (!owner || !owner->registry || !registration || registration->size != sizeof(trinity_service_registration) || !registration->service)
        return -1;
    if (!owner->registry->services.emplace(registration->service_id, ServiceEntry{registration->service, owner}).second)
        return -2;
    Registration value;
    value.service = *registration;
    value.serviceName = registration->name ? registration->name : "";
    value.service.name = value.serviceName.c_str();
    owner->registrations.emplace_back(std::move(value));
    return 0;
}
static int32_t RegisterCommand(trinity_host* host, trinity_command_registration const* registration)
{
    ModuleOwner* owner = Owner(host);
    if (!owner || !registration || registration->size != sizeof(trinity_command_registration) || !registration->execute || !registration->name || !*registration->name)
        return -1;
    Registration value;
    value.command = *registration;
    value.commandName = registration->name;
    value.help = registration->help ? registration->help : "";
    value.command.name = value.commandName.c_str();
    value.command.help = value.help.c_str();
    owner->registrations.emplace_back(std::move(value));
    return 0;
}
static int32_t RegisterFamily(trinity_host* host, trinity_family_id id, void const* callbacks)
{
    ModuleOwner* owner = Owner(host);
    if (!owner || !callbacks)
        return -1;
    if (id == TRINITY_FAMILY_WORLD)
    {
        auto const* table = static_cast<trinity_world_callbacks const*>(callbacks);
        if (table->size != sizeof(trinity_world_callbacks) || table->host != &owner->host || owner->worldScript)
            return -1;
        owner->worldCallbacks = *table;
        owner->worldScript = new ModuleWorldScript(owner, *table);
        return 0;
    }
    if (id == TRINITY_FAMILY_SPELL_LOADER)
    {
        auto const* loader = static_cast<trinity_spell_loader_callbacks const*>(callbacks);
        if (loader->size != sizeof(trinity_spell_loader_callbacks) || loader->abi_version != TRINITY_ABI_V1 || (!loader->spell && !loader->aura))
            return -1;
        std::string error;
        return Trinity::Modules::RegisterSpellAuraAdapters(&owner->host, loader->spell, loader->aura, error) ? 0 : -1;
    }
    std::string error;
    return RegisterAbiFamily(&owner->host, static_cast<uint32>(id), callbacks, error) ? 0 : -1;
}
static void* GetService(trinity_host* host, uint32_t serviceId)
{
    ModuleOwner* owner = Owner(host);
    if (!owner || !owner->registry)
        return nullptr;
    auto itr = owner->registry->services.find(serviceId);
    return itr == owner->registry->services.end() ? nullptr : itr->second.value;
}
static void* GetCapability(trinity_host* host, char const* name)
{
    if (!name || !Owner(host))
        return nullptr;
    if (std::strcmp(name, TRINITY_CAPABILITY_CONFIG_V1) == 0)
    {
        static trinity_config_api_v1 config{sizeof(trinity_config_api_v1), TRINITY_ABI_V1, GetConfigString};
        return &config;
    }
    if (std::strcmp(name, TRINITY_CAPABILITY_COMMAND_V1) == 0)
    {
        static trinity_command_api_v1 command{sizeof(trinity_command_api_v1), TRINITY_ABI_V1, SendSysMessage, IsConsole};
        return &command;
    }
    if (std::strcmp(name, TRINITY_CAPABILITY_OBJECT_V1) == 0)
        return const_cast<trinity_object_api_v1*>(GetAbiObjectApi());
    return nullptr;
}
}
struct ModuleMgr::Impl
{
    struct Loaded { Manifest manifest; std::unique_ptr<Handle> handle; std::unique_ptr<ModuleOwner> owner; };
    ModuleRegistry registry;
    std::vector<Manifest> discovered;
    std::vector<Loaded> loaded;
    boost::filesystem::path root;
    bool discoveredDone = false;
    bool loadedDone = false;
};
ModuleMgr::ModuleMgr() : _impl(std::make_unique<Impl>()) { }
ModuleMgr::~ModuleMgr() = default;
ModuleMgr* ModuleMgr::instance() { static ModuleMgr manager; return &manager; }
void ModuleMgr::Discover(std::string const& moduleDir)
{
    if (_impl->discoveredDone)
        return;
    _impl->root = boost::filesystem::absolute(moduleDir.empty() ? "modules" : moduleDir);
    _impl->discoveredDone = true;
    for (auto const& entry : boost::filesystem::directory_iterator(_impl->root))
    {
        if (!boost::filesystem::is_directory(entry.path()) || boost::filesystem::is_symlink(entry.path()))
            continue;
        Manifest value;
        boost::filesystem::path manifestPath = entry.path() / "module.json";
        if (!boost::filesystem::is_regular_file(manifestPath) || boost::filesystem::is_symlink(manifestPath) || !ParseManifest(manifestPath, value))
            throw std::runtime_error("invalid module manifest: " + manifestPath.string());
        if (entry.path().filename().string() != value.name)
            throw std::runtime_error("module directory does not match manifest name: " + manifestPath.string());
        _impl->discovered.push_back(std::move(value));
    }
    std::unordered_set<std::string> names;
    for (Manifest const& manifest : _impl->discovered)
        if (!names.emplace(manifest.name).second)
            throw std::runtime_error("duplicate module name: " + manifest.name);
    std::stable_sort(_impl->discovered.begin(), _impl->discovered.end(), [](Manifest const& left, Manifest const& right) { return left.loadOrder != right.loadOrder ? left.loadOrder < right.loadOrder : left.name < right.name; });
    TC_LOG_INFO("server.modules", "Discovered {} module manifest(s)", _impl->discovered.size());
}
bool ModuleMgr::Load()
{
    if (_impl->loadedDone)
        return true;
    Discover();
    std::unordered_map<std::string, size_t> indices;
    for (size_t i = 0; i < _impl->discovered.size(); ++i)
        indices.emplace(_impl->discovered[i].name, i);
    std::vector<size_t> order;
    std::set<size_t> visiting, visited;
    std::function<bool(size_t)> visit = [&](size_t index)
    {
        if (visited.contains(index)) return true;
        if (!visiting.insert(index).second) return false;
        for (auto const& dependency : _impl->discovered[index].depends)
        {
            auto itr = indices.find(DependencyName(dependency));
            if (itr == indices.end() || !DependencySatisfied(dependency, _impl->discovered[itr->second]) || !visit(itr->second)) return false;
        }
        visiting.erase(index); visited.insert(index); order.push_back(index); return true;
    };
    for (size_t i = 0; i < _impl->discovered.size(); ++i)
        if (!visit(i))
            throw std::runtime_error("module dependency cycle, missing dependency, or incompatible version");
    for (size_t index : order)
    {
        Manifest const& manifest = _impl->discovered[index];
        auto directory = boost::filesystem::path(manifest.path).parent_path();
#if defined(_WIN32)
        auto library = directory / (manifest.name + ".dll");
        HMODULE raw = nullptr;
#else
        auto library = directory / (manifest.name + ".so");
        void* raw = nullptr;
#endif
        if (!VerifyPackage(manifest, library)) throw std::runtime_error("module package verification failed: " + manifest.name);
#if defined(_WIN32)
        raw = LoadLibraryW(library.wstring().c_str());
#else
        raw = dlopen(library.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (!raw) throw std::runtime_error("unable to load module library " + library.string());
        auto handle = std::make_unique<Handle>(); handle->value = raw;
#if defined(_WIN32)
        auto entry = reinterpret_cast<trinity_module_get_api_v1>(GetProcAddress(raw, "TrinityModuleGetApi"));
#else
        auto entry = reinterpret_cast<trinity_module_get_api_v1>(dlsym(raw, "TrinityModuleGetApi"));
#endif
        if (!entry) throw std::runtime_error("module does not export TrinityModuleGetApi: " + manifest.name);
        auto owner = std::make_unique<ModuleOwner>();
        owner->registry = &_impl->registry; owner->module.owner = owner.get(); owner->host.owner = owner.get(); owner->contextName = "___module_" + manifest.name;
        owner->query = {sizeof(trinity_host_query), TRINITY_ABI_V1, owner.get(), HostQuery, RegisterService, GetService, RegisterCommand, RegisterFamily, GetCapability};
        owner->log = {sizeof(trinity_log_api), HostLog}; owner->hostApi = {sizeof(trinity_host_api_v1), TRINITY_ABI_V1, owner->query, owner->log}; owner->host.api = owner->hostApi;
        owner->api.size = sizeof(trinity_module_api); owner->api.abi_version = TRINITY_ABI_V1;
        if (entry(TRINITY_ABI_V1, &owner->api) != 0 || owner->api.abi_version != TRINITY_ABI_V1 || owner->api.size != sizeof(trinity_module_api) || !owner->api.on_load)
            throw std::runtime_error("module API is missing required v1 lifecycle: " + manifest.name);
        owner->moduleContext = owner->api.module_context;
        _impl->loaded.push_back({manifest, std::move(handle), std::move(owner)});
        ModuleOwner* loadedOwner = _impl->loaded.back().owner.get();
        sScriptMgr->SetScriptContext(loadedOwner->contextName);
        if (loadedOwner->api.on_load(loadedOwner->moduleContext, &loadedOwner->host) != 0)
            throw std::runtime_error("module on_load failed: " + manifest.name);
        if (!loadedOwner->registrations.empty())
            loadedOwner->commandScript = new ModuleCommandScript(loadedOwner);
    }
    sScriptMgr->SwapScriptContext();
    _impl->loadedDone = true;
    return true;
}
void ModuleMgr::Unload()
{
    for (auto itr = _impl->loaded.rbegin(); itr != _impl->loaded.rend(); ++itr)
    {
        ModuleOwner* owner = itr->owner.get();
        if (!owner) continue;
        sScriptMgr->ReleaseScriptContext(owner->contextName);
        sScriptMgr->SwapScriptContext();
        Trinity::ChatCommands::InvalidateCommandMap();
        InvalidateAbiHost(&owner->host);
        if (owner->api.on_unload) owner->api.on_unload(owner->moduleContext, &owner->host);
        for (auto service = _impl->registry.services.begin(); service != _impl->registry.services.end();)
            if (service->second.owner == owner) service = _impl->registry.services.erase(service); else ++service;
    }
    _impl->loaded.clear();
    _impl->loadedDone = false;
}
void ModuleMgr::OnConfigReload()
{
    for (auto& module : _impl->loaded)
        if (module.owner->api.on_config_reload)
            module.owner->api.on_config_reload(module.owner->moduleContext, &module.owner->host);
}
bool ModuleMgr::RegisterDatabaseUpdates(std::string const& dbName)
{
    if (dbName == "all")
        return true;
    for (auto const& module : _impl->discovered)
    {
        std::string database = dbName;
        std::transform(database.begin(), database.end(), database.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (std::find(module.databases.begin(), module.databases.end(), database) == module.databases.end())
            continue;
        auto path = boost::filesystem::path(module.path).parent_path() / "sql" / database;
        if (!boost::filesystem::is_directory(path))
            return false;
        boost::filesystem::path library = boost::filesystem::path(module.path).parent_path() / (module.name + "." +
#if defined(_WIN32)
            "dll"
#else
            "so"
#endif
            );
        if (!VerifyPackage(module, library))
            return false;
        std::string escaped = boost::filesystem::absolute(path).generic_string();
        if (dbName == "World") WorldDatabase.EscapeString(escaped);
        else if (dbName == "Character") CharacterDatabase.EscapeString(escaped);
        else if (dbName == "Login") LoginDatabase.EscapeString(escaped);
        else if (dbName == "Hotfix") HotfixDatabase.EscapeString(escaped);
        else return false;
        if (dbName == "World") WorldDatabase.DirectPExecute("INSERT IGNORE INTO updates_include (path, state) VALUES ('%s', 'RELEASED')", escaped.c_str());
    }
    return true;
}
std::vector<std::string> ModuleMgr::GetLoadedModuleInfo() const
{
    std::vector<std::string> result;
    for (auto const& module : _impl->loaded) result.push_back(Trinity::StringFormat("{} v{}", module.manifest.name, module.manifest.version));
    return result;
}
std::string ModuleMgr::GetModuleInfo(std::string const& name) const
{
    for (auto const& module : _impl->loaded) if (module.manifest.name == name) return Trinity::StringFormat("{} v{}", module.manifest.name, module.manifest.version);
    return {};
}
bool ModuleMgr::OwnsContext(std::string const& context) const
{
    for (auto const& module : _impl->loaded) if (module.owner->contextName == context) return true;
    return false;
}
}
