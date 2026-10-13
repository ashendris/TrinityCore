# TrinityCore Module SDK

The module SDK is a source-level contract with a strict, core-commit-pinned binary ABI. Modules are dynamically loaded shared libraries (there is no static module mode) and are rebuilt for the exact worldserver commit that loads them. Hot reload is not part of this initial SDK.

## Public entry point

Include the installed SDK header:

```cpp
#include "TrinityModule.h"

class MyModule final : public Trinity::Module
{
public:
    void OnLoad(Trinity::ModuleContext& ctx) override;
    void OnUnload() override;
    void OnConfigReload() override;
};

TC_MODULE(MyModule, "my-module", "1.0.0", R"json({
  "name": "my-module",
  "version": "1.0.0",
  "sdk": ">=1.0 <2.0",
  "depends": [],
  "databases": ["world"],
  "config": "conf/my-module.conf"
})json");
```

`TC_MODULE` is the only required entry point. It exports the ABI, manifest, create, and destroy functions of the shared library. The runtime validates the sidecar `module.json` before loading a shared library and validates strict ABI metadata before invoking its factory.

The generated `TrinitySDKBuild.h` is included by both core and module builds. ABI metadata includes SDK major/minor, core revision, compiler identity, C++ runtime/CRT and debug token, and generated SDK header hash. A mismatch is a hard load refusal; there is no warning-only compatibility mode.

## Context services

`OnLoad` is called after the module's dependencies have loaded. Use the context rather than global configuration or logging categories:

```cpp
int limit = ctx.Config().GetInt("Limit", 10);
bool enabled = ctx.Config().GetBool("Enabled", true);
std::string label = ctx.Config().GetString("Label", "default");
ctx.Log().Info("loaded {}", label);
ctx.Log().Warn("warning");
ctx.Log().Error("error");
ctx.AddScript<MyWorldScript>();
ctx.Services().Provide<IMyService>(std::make_shared<MyService>());
auto service = ctx.Services().Require<IMyService>();
```

Configuration is namespaced as `Module.<manifest-name>.<key>`. A module's distributed config uses the `[worldserver]` section and should be copied into the active worldserver config path. The initial PoC reads values during `OnLoad`; changing them requires a worldserver restart. `OnConfigReload` is reserved for the runtime's later reload implementation.

Script objects remain normal `WorldScript`, `CommandScript`, and other existing `ScriptObject` subclasses. `ctx.AddScript<T>()` registers them in the module's script context. Modules MUST NOT call `SwapScriptContext` themselves.

## Manifest and resources

A manifest (`module.json`) is JSON and MUST contain a semantic `name` and `version`, an SDK range, dependency constraints, and any declared database/config resources:

```json
{
  "name": "my-module",
  "version": "1.0.0",
  "sdk": ">=1.0 <2.0",
  "depends": ["provider >=1.0"],
  "load_order": 100,
  "databases": ["world"],
  "config": "conf/my-module.conf"
}
```

Runtime resources live under the worldserver `modules` directory (`bin/<config>/modules/` in the build tree):

```text
modules/<name>/module.json
modules/<name>/<name>.dll       # Windows
modules/<name>/<name>.so        # Linux
modules/<name>/conf/*.conf.dist
modules/<name>/sql/<database>/*.sql
```

SQL is considered only for databases listed by the manifest. Update filenames MUST be unique across modules; rows MUST be scoped to module-owned values. `trinity_add_module` copies the manifest and resource files next to the shared library.

## Building in-tree

The PoC modules are `modules/mod-greeting` and `modules/mod-example`. Modules are dynamic-only. Enable each with its CMake variable; the accepted values are `dynamic` and `disabled`:

```text
cmake -S . -B build -DMODULE_MOD_GREETING=dynamic -DMODULE_MOD_EXAMPLE=dynamic
```

The variable name is `MODULE_<NAME>` with the module name upper-cased and `-` replaced by `_`. After the build, the outputs are:

```text
bin/<config>/modules/mod-greeting/mod-greeting.dll   # or .so on Linux
bin/<config>/modules/mod-greeting/module.json
bin/<config>/modules/mod-example/mod-example.dll     # or .so on Linux
bin/<config>/modules/mod-example/module.json
```

Their CMake files use:

```cmake
find_package(TrinitySDK CONFIG REQUIRED)
trinity_add_module(NAME mod-example
  SOURCES src/mod_example.cpp
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/module.json
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/conf/mod-example.conf.dist
  SQL ${CMAKE_CURRENT_SOURCE_DIR}/sql/world/mod_example_001.sql
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../mod-greeting/include)
```

`mod-greeting` provides `GreetingService` from `include/GreetingService.h`; `mod-example` declares `mod-greeting >=1.0` and requires that service. The examples demonstrate startup/shutdown hooks, namespaced config, prefixed logging, dependency ordering, and an RBAC-protected GM/console command.

## Building against an installed SDK

Install the core SDK package and configure an external CMake project with `CMAKE_PREFIX_PATH` pointing at the SDK install prefix:

```cmake
find_package(TrinitySDK CONFIG REQUIRED)
trinity_add_module(NAME my-module
  SOURCES src/my_module.cpp
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/module.json
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/conf/my-module.conf.dist
  SQL ${CMAKE_CURRENT_SOURCE_DIR}/sql/world/my_module_001.sql)
```

For a provider/consumer pair, install/export the provider's public headers and pass the provider include directory through `INCLUDE_DIRS`; do not include private core source paths. Build the module with the same compiler, standard library/CRT, debug/release setting, and exact core revision recorded by the installed SDK.

## PoC command

With both example modules loaded, a worldserver console or an account possessing the existing `RBAC_PERM_COMMAND_GM` permission can run:

```text
.module example hello
```

The response is generated by `mod-greeting`'s service and uses `Module.mod-example.Subject`. No new RBAC ID is introduced and ordinary players cannot invoke it.

## CI and release policy

Every core commit is an ABI boundary. CI SHOULD rebuild modules against each supported core commit and publish artifacts named with commit, platform, compiler, CRT/runtime, and configuration, for example:

```text
mod-example-<commit>-linux-gcc13-libstdc++-Release.so
mod-example-<commit>-windows-msvc143-ucrt-RelWithDebInfo.dll
```

The module SDK workflow builds Linux dynamic variants (GCC and Clang) and a Windows dynamic variant where the repository dependencies are available, installs the core SDK package, then configures and builds an external PoC against that installed package. The workflow records compiler/CRT/configuration in artifact names and fails if the external build or ABI metadata generation fails. A failed module rebuild is release-blocking feedback: publish no artifact for that core commit and fix the compatibility/build issue before distribution.

## Verification status

Shared-library runtime loading and hard ABI refusal, live-database SQL idempotence, and a fresh installed-SDK external build require the shared-core and MySQL development environments; those paths are documented but are not claimed as verified by this change.
