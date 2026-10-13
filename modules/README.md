# Worldserver modules

Modules are dynamically loaded shared libraries. There is no static module mode: a module is either built as a shared library (`dynamic`) or not built (`disabled`). `mod-greeting` and `mod-example` are small in-tree proof-of-concept modules. They use the same SDK and manifest contract as an installed, out-of-tree module:

```text
modules/<name>/
  module.json
  CMakeLists.txt
  src/
  include/                 # optional public headers
  conf/<name>.conf.dist
  sql/<database>/<name>_001.sql
```

## Building the examples

Each module is selected with a `MODULE_<NAME>` CMake variable (the module name upper-cased, `-` replaced by `_`). The only accepted values are `dynamic` and `disabled`:

```text
-DMODULE_MOD_GREETING=dynamic -DMODULE_MOD_EXAMPLE=dynamic
```

The core build exports the `TrinitySDK` CMake package and the `trinity_add_module()` helper. Configure in-tree with the flags above, or build an external module against an installed SDK:

```cmake
find_package(TrinitySDK CONFIG REQUIRED)
trinity_add_module(NAME my-module
  SOURCES src/my_module.cpp
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/module.json
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/conf/my-module.conf.dist
  SQL ${CMAKE_CURRENT_SOURCE_DIR}/sql/world/my_module_001.sql)
```

For an in-tree build, `modules/mod-example` explicitly adds `modules/mod-greeting/include` so the consumer can include `GreetingService.h`. An external project should install/export the provider's public include directory (or copy the header into its own SDK-facing include tree) and add it to `INCLUDE_DIRS`.

`trinity_add_module()` accepts `NAME`, `SOURCES`, `MANIFEST`, `CONFIG`, `SQL`, and optional `INCLUDE_DIRS`. It emits the module layout expected by worldserver, under `bin/<config>/` in the build tree (and the matching location in the install tree):

```text
bin/<config>/modules/<name>/<name>.dll       # Windows
bin/<config>/modules/<name>/<name>.so        # Linux
bin/<config>/modules/<name>/module.json
bin/<config>/modules/<name>/conf/*.conf.dist
bin/<config>/modules/<name>/sql/<database>/*.sql
```

Every module places its loadable library beside its `module.json` sidecar. The `TC_MODULE(Type, name, version, manifest)` entry point exports the SDK ABI, manifest, create, and destroy functions. Module discovery validates the sidecar manifest and strict core-commit ABI before calling the factory.

## Running the PoC

1. Copy the distributed config into the active worldserver config directory (or use the normal additional-config search path):
   `conf/mod-greeting.conf.dist` and `conf/mod-example.conf.dist`.
2. Keep the `[worldserver]` section and edit `Module.mod-greeting.Prefix` or `Module.mod-example.Subject`. Config keys are namespaced as `Module.<name>.<key>`.
3. Start worldserver with both modules present in its `modules/` directory. `mod-example` declares `mod-greeting >=1.0`, so the manager loads the provider first.
4. As an account with the existing `RBAC_PERM_COMMAND_GM` permission, or from the worldserver console, run:
   `.module example hello`

The command returns a greeting produced by `mod-greeting`'s `GreetingService`; it is not a public command and does not create a new RBAC permission. Startup and shutdown lifecycle messages use the `module.mod-greeting` and `module.mod-example` log categories.

Configuration is read from the namespaced keys during `OnLoad`; restart worldserver after changing these values. Hot reload/config-refresh behavior is intentionally outside this PoC.

## SDK and compatibility policy

The SDK is source-level plus version-pinned binary compatibility. A module MUST be rebuilt for every core commit it runs with. The ABI gate includes the SDK version, compiler identity, C++ standard library/CRT and debug token, generated SDK header hash, and core revision; a mismatch is a hard load failure before module code runs. Do not ship a module binary across core commits.

The module manifest MUST use the declared SDK range (the examples use `>=1.0 <2.0`), semantic version, dependency constraints, config path, and database list. Database SQL is applied only for databases declared in the manifest and should use unique filenames and scoped rows.
