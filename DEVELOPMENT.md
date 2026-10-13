# Development

## Module SDK development

The module SDK and runtime currently live on the `feature/plugin-system` branch. Use that branch when developing, building, and testing modules until the module system is merged into another branch.

Build the core and install its SDK from the same source revision used by the server:

```sh
cmake -S /path/to/TrinityCore \
      -B /path/to/core-build \
      -DCMAKE_INSTALL_PREFIX=/path/to/trinitycore-sdk

cmake --build /path/to/core-build --config RelWithDebInfo
cmake --install /path/to/core-build --config RelWithDebInfo
```

Configure an external module against that installed SDK:

```sh
cmake -S /path/to/module \
      -B /path/to/module-build \
      -DCMAKE_PREFIX_PATH=/path/to/trinitycore-sdk

cmake --build /path/to/module-build --config RelWithDebInfo
```

The module and the server MUST use a matching or compatible SDK revision. Rebuild the module whenever the SDK ABI changes. Modules SHOULD link only against the public `Trinity::SDK` target and MUST NOT depend on private `game` or `worldserver` targets.

Do not build a module against `master` and run it with `feature/plugin-system`, or mix SDK and server artifacts from different revisions, unless compatibility has been explicitly verified.

See [`doc/ModuleSDK.md`](doc/ModuleSDK.md) for the SDK API, ABI, loading, and packaging requirements.
