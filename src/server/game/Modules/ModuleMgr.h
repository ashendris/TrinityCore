/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#ifndef TRINITYCORE_MODULEMGR_H
#define TRINITYCORE_MODULEMGR_H
#include "TrinityModule.h"
#include <memory>
#include <string>
#include <vector>
namespace Trinity
{
class TC_GAME_API ModuleMgr
{
public:
    static ModuleMgr* instance();
    void Discover(std::string const& moduleDir = {});
    bool Load();
    void Unload();
    void OnConfigReload();
    bool RegisterDatabaseUpdates(std::string const& dbName);
    std::vector<std::string> GetLoadedModuleInfo() const;
    std::string GetModuleInfo(std::string const& name) const;
    bool OwnsContext(std::string const& context) const;
private:
    ModuleMgr();
    ~ModuleMgr();
    ModuleMgr(ModuleMgr const&) = delete;
    ModuleMgr& operator=(ModuleMgr const&) = delete;
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
#define sModuleMgr Trinity::ModuleMgr::instance()
#endif
