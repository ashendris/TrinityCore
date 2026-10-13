# TrinityCore Module SDK

The module SDK is a versioned C ABI. Modules are dynamically loaded shared libraries and MUST use only SDK-owned headers. The loader negotiates the ABI version and capabilities before invoking a module. Native modules are trusted in-process code; production packages MUST carry a valid Ed25519 signature and library digest.

## Public entry point

Include the installed SDK header:

```cpp
#include "TrinityModule.h"

static int32_t OnLoad(trinity_host const* host)
{
    trinity_host_api_v1 const* api = TrinityHostApiV1(host);
    if (!api || api->abi_version != TRINITY_ABI_V1)
        return -1;
    api->log.write(const_cast<trinity_host*>(host), 1, "module loaded", 13);
    return 0;
}

extern "C" int32_t TrinityModuleGetApi(uint32_t requested_abi, trinity_module_api* out_api)
{
    if (requested_abi != TRINITY_ABI_V1 || !out_api)
        return -1;
    trinity_module_api api{};
    api.size = sizeof(api);
    api.abi_version = TRINITY_ABI_V1;
    api.on_load = OnLoad;
    *out_api = api;
    return 0;
}
```

The generated header defines the stable module API, host query tables, capability IDs, opaque handles, and explicit script-family callback tables. A module MUST validate structure `size` fields before accessing optional fields. Unknown or unsupported capabilities MUST be rejected rather than assumed.

## Host capabilities

The v1 host exposes versioned tables for:

- logging;
- caller-owned configuration buffers;
- command registration, permissions, console access, and command replies;
- service registration and dependency lookup;
- world lifecycle callbacks;
- generated script-family callback declarations.

Object pointers MUST NOT cross the boundary. Script callback operands are opaque handles and are valid only for the callback unless a host table documents a longer lifetime.

The runtime currently dispatches the world callback family and command bridge. Other generated family tables are schema declarations until their corresponding host adapter is enabled; modules MUST NOT register an unsupported family.

## Manifest and resources

The manifest is authenticated before any native library, configuration, or SQL is processed. The signed `files` object maps every regular package file (native binary, configuration/default files, and SQL) to its SHA-256 digest; symlinks and files outside the package directory are rejected. The canonical signing bytes are compact UTF-8 JSON with `signature` removed and sorted keys, matching `sign_module.py`. Trusted keys are raw 32-byte or 64-hex-byte files named `<key_id>.pub` below `Modules.TrustedKeyDirectory`; key IDs and package names are restricted to safe basenames. Operators MUST treat the configured modules and trusted-key directories as a deployment boundary writable only by trusted administrators.

```json
{
  "name": "my-module", "version": "1.0.0", "sdk": ">=1.0 <2.0",
  "depends": [], "load_order": 100, "databases": ["world"],
  "key_id": "publisher-2026", "files": {"my-module.so": "<sha256>"},
  "signature": "<64-byte Ed25519 signature, hex>"
}
```

Resources live under `modules/<name>` and are covered by `files`:

```text
modules/<name>/module.json
modules/<name>/<name>.dll       # Windows
modules/<name>/<name>.so        # Linux
modules/<name>/conf/*.conf.dist
modules/<name>/sql/<database>/*.sql
```

## Building

Build and install the SDK from a matching TrinityCore build, then use it from an external module repository:

```sh
cmake -S /path/to/TrinityCore -B /path/to/core-build \
  -DCMAKE_INSTALL_PREFIX=/path/to/trinitycore-sdk
cmake --build /path/to/core-build --config RelWithDebInfo
cmake --install /path/to/core-build --config RelWithDebInfo

cmake -S /path/to/module -B /path/to/module-build \
  -DCMAKE_PREFIX_PATH=/path/to/trinitycore-sdk
cmake --build /path/to/module-build --config RelWithDebInfo
```

An external project uses:

```cmake
find_package(TrinitySDK CONFIG REQUIRED)
trinity_add_module(NAME my-module
  SOURCES src/my_module.cpp
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/module.json
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/conf/my-module.conf.dist
  SQL ${CMAKE_CURRENT_SOURCE_DIR}/sql/world/my_module_001.sql)
```

`trinity_add_module()` links only `Trinity::SDK`; it MUST NOT link `game`, `worldserver`, or private TrinityCore targets.

External examples are maintained in the separate `TrinityCoreModulesExamples` repository. For creation, build, test, package, deploy, upgrade, and CI procedures, see its [development guide](https://github.com/TrinityCore/TrinityCoreModulesExamples/blob/master/DEVELOPMENT.md).

## Compatibility and security

To sign a final package, use the SDK `sign_module.py` tool with `--manifest`, `--library`, `--package-root`, `--private-key`, and `--key-id`. Install only the resulting public key as `keys/<key_id>.pub` on the server. See the [end-to-end module guide](https://github.com/TrinityCore/TrinityCoreModulesExamples/blob/master/DEVELOPMENT.md) for exact Windows/Linux commands and runtime verification.

The module ABI version and capabilities, not arbitrary core C++ layout, define compatibility. A major ABI change requires a new ABI version. Native modules still have full server privileges. Signature verification authenticates the publisher and protects package integrity; it is not a sandbox.
