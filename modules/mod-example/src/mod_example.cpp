#include "TrinityModule.h"
#include "GreetingService.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ScriptMgr.h"
#include "RBAC.h"
#include <memory>
#include <string>
using namespace Trinity::ChatCommands;
namespace
{
std::string subject;
std::shared_ptr<Trinity::Modules::GreetingService> greeting;
class ExampleWorldScript final : public WorldScript
{
public:
    ExampleWorldScript() : WorldScript("mod-example-world") { }
    void OnStartup() override { TC_LOG_INFO("module.mod-example", "mod-example started for {}.", subject); }
    void OnShutdown() override { TC_LOG_INFO("module.mod-example", "mod-example stopped."); }
};
class ExampleCommandScript final : public CommandScript
{
public:
    ExampleCommandScript() : CommandScript("mod-example-commands") { }
    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable commands = {{"hello", HandleHello, rbac::RBAC_PERM_COMMAND_GM, Console::Yes}};
        static ChatCommandTable example = {{"example", commands}};
        static ChatCommandTable root = {{"module", example}};
        return root;
    }
private:
    static bool HandleHello(ChatHandler* handler)
    {
        handler->SendSysMessage(greeting->Greeting(subject));
        return true;
    }
};
}
class ExampleModule final : public Trinity::Module
{
public:
    void OnLoad(Trinity::ModuleContext& context) override
    {
        subject = context.Config().GetString("Subject", "module SDK");
        greeting = context.Services().Require<Trinity::Modules::GreetingService>();
        context.AddScript<ExampleWorldScript>();
        context.AddScript<ExampleCommandScript>();
        context.Log().Info("loaded; use '.module example hello' as a GM or console.");
    }
    void OnUnload() override { greeting.reset(); subject.clear(); }
};
TC_MODULE(ExampleModule)
