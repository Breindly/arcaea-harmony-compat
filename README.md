# Harmony Rhythm Compatibility Layer

[English](README.md) | [简体中文](README.zh-CN.md)

An open-source HarmonyOS compatibility and rhythm-game prototype. The project
contains independently maintained parsing, playback, rendering and platform
integration code.

This repository does **not** include or grant rights to Arcaea game binaries,
music, charts, artwork, characters, fonts, models, extracted assets, account
data or unlock mechanisms. Arcaea and related marks belong to their respective
owners. This project is not affiliated with or endorsed by lowiro.

## Scope

- AFF chart parser and playback timeline
- Note judging and Recollection Rate calculation
- ArkUI song-selection, play and result screens
- HarmonyOS NAPI, OpenGL and audio integration
- Version-profile schemas and compatibility documentation
- Tools for importing resources that the user is legally allowed to use

## Repository boundaries

The following must never be committed:

- APK, HAP, ZIP or extracted game payloads
- Official audio, charts, jackets, fonts, models or other game resources
- Proprietary native libraries or decompiled source
- Save files, account identifiers, device identifiers or crash dumps
- Code whose purpose is bypassing licensing, anti-tamper checks or paid unlocks

Read [docs/ASSETS.md](docs/ASSETS.md) before running any import tool.

## Requirements

- DevEco Studio with HarmonyOS SDK API 26
- Node.js bundled with DevEco Studio
- Hvigor 6.26.x
- An arm64 HarmonyOS device or emulator for runtime testing

## Build

Open the repository in DevEco Studio and let it install the local SDK
dependencies. Configure a local debug signing profile in
`File > Project Structure > Signing Configs`; no signing keys are distributed
with this repository. Then run:

```powershell
.\build.ps1
```

Set `DEVECO_HOME` when DevEco Studio is installed somewhere other than
`D:\DevEco Studio`.

The signed package is always named `ArcHarmony.hap`. The unsigned intermediate
is named `ArcHarmony-unsigned.hap`.

## Optional resource import

`tools/build_library.ps1` generates chart and metadata files from a directory
that the user is authorized to use. The tool has no bundled resource path and
requires `-SongsRoot`.

`tools/make_audio_zip.ps1` packages user-owned audio into a separately imported
ZIP. The generated ZIP is ignored by Git.

The repository starts with an empty library manifest so it remains distributable
without third-party content.

## Versioning

Compatibility information is split into three layers:

1. Version Profile: version, ABI, dependency and resource metadata.
2. Analysis Profile: local, version-specific observations and match evidence.
3. Compatibility Logic: source code and tests implementing behavior.

See [docs/VERSIONING.md](docs/VERSIONING.md) and
[profiles/schema/version-profile.schema.json](profiles/schema/version-profile.schema.json).

## Contributing

Contributions are accepted under the Apache License 2.0 with Developer
Certificate of Origin sign-off. Read [CONTRIBUTING.md](CONTRIBUTING.md) and
[SECURITY.md](SECURITY.md).

Run the repository boundary check before every commit:

```powershell
.\tools\check-open-source-boundary.ps1
```

## License

Original source code in this repository is licensed under the Apache License
2.0. Third-party names, game content and trademarks are not covered by this
license.
