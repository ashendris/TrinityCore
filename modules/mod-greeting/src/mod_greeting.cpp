#include "TrinityModule.h"
#include "GreetingService.h"
#include "ScriptMgr.h"
#include "Log.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{
class GreetingServiceImpl final : public Trinity::Modules::GreetingService
{
public:
    explicit GreetingServiceImpl(std::string prefix) : _prefix(std::move(prefix)) { }
    std::string Greeting(std::string_view subject) const override
    {
        return _prefix + ", " + std::string(subject) + "! mod-greeting is ready.";
    }
private:
    std::string _prefix;
};
std::shared_ptr<Trinity::Modules::GreetingService> greeting;
std::string prefix;
class GreetingWorldScript final : public WorldScript
{
public:
    GreetingWorldScript() : WorldScript("mod-greeting-world") { }
    void OnStartup() override { TC_LOG_INFO("module.mod-greeting", "{}", greeting->Greeting("world")); }
    void OnShutdown() override { TC_LOG_INFO("module.mod-greeting", "{} is shutting down.", prefix); }
};
}
class GreetingModule final : public Trinity::Module
{
public:
    void OnLoad(Trinity::ModuleContext& context) override
    {
        prefix = context.Config().GetString("Prefix", "Hello");
        greeting = std::make_shared<GreetingServiceImpl>(prefix);
        context.Services().Provide<Trinity::Modules::GreetingService>(greeting);
        context.AddScript<GreetingWorldScript>();
        context.Log().Info("loaded with prefix '{}'.", prefix);
    }
    void OnUnload() override { greeting.reset(); prefix.clear(); }
};
TC_MODULE(GreetingModule)
