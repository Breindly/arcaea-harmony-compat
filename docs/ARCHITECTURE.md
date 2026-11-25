# Architecture

## Layers

### `core/`

Portable C++ code for chart parsing, timing, playback, judging and mathematical
helpers. This layer has no HarmonyOS UI dependency.

### `entry/src/main/cpp/`

HarmonyOS native integration:

- NAPI entry points
- OpenGL ES renderer
- Audio playback through HarmonyOS native media APIs

### `entry/src/main/ets/`

ArkUI application:

- Library and song selection
- Play screen and controls
- Result and persistent settings
- Resource import UI

### `profiles/`

Version metadata schemas and synthetic examples. These files describe versions
and compatibility requirements without containing game content.

## Data flow

1. A user selects a legally usable library or creates one with an import tool.
2. ArkUI reads the generated manifest.
3. NAPI passes chart and texture data to the native core.
4. The native layer parses, schedules and renders the chart.
5. Judgement results return to ArkUI for persistence and display.

## Trust boundaries

- Treat imported archives and manifests as untrusted input.
- Validate paths, sizes and numeric ranges.
- Keep native parsing bounds-checked.
- Never execute files from imported payloads.
