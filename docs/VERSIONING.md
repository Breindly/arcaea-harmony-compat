# Versioning Strategy

Version updates are represented in three layers.

## 1. Version Profile

Stable, data-friendly metadata:

- Version name and code
- Artifact SHA-256
- ABI and dependency requirements
- Minimum HarmonyOS API
- Public resource-schema version

Schema:
[profiles/schema/version-profile.schema.json](../profiles/schema/version-profile.schema.json)

## 2. Analysis Profile

Version-specific observations used during local investigation:

- Interface candidates
- Match evidence
- Original instruction or symbol checks
- Verification status

Analysis Profiles may contain derived technical information. Publish only the
parts that are legally safe and necessary for interoperability. Do not commit
proprietary binaries or copied implementation text.

## 3. Compatibility Logic

Behavior belongs in source code, not configuration:

- JNI or NAPI behavior
- Initialization sequencing
- Audio and rendering differences
- Gameplay behavior adaptations

Every behavior change needs a regression test. Configuration should describe
the environment; it should not become an arbitrary script for code patching.

## Release flow

1. Add a Version Profile for the target build.
2. Record compatibility requirements in the release notes.
3. Update Compatibility Logic with tests.
4. Run the boundary check and build.
5. Publish source and synthetic fixtures only.
