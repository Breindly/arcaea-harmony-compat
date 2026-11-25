# Contributing

## Allowed contributions

- Original implementations and tests
- HarmonyOS API compatibility code
- Publicly documented format parsers
- Synthetic fixtures created by the contributor
- Build, packaging and documentation improvements

## Prohibited contributions

- Game APKs, HAPs, payloads or extracted resources
- Official music, charts, artwork, fonts, models or story text
- Decompiled proprietary source or redistributed proprietary binaries
- Save files, account data, device identifiers or crash dumps
- License, payment, integrity-check or paid-content bypasses
- Secrets, signing private keys or personal absolute paths

## Provenance

Each pull request must state how the code was produced. If a change was informed
by binary analysis, describe the observed interface or behavior without copying
proprietary implementation text or embedding redistributable binary data.

## Developer Certificate of Origin

Sign off every commit:

```text
Signed-off-by: Your Name <you@example.com>
```

Use:

```bash
git commit -s
```

By signing off, you certify that you have the right to submit the contribution
under the project license.

## Quality checks

```powershell
.\tools\check-open-source-boundary.ps1
.\build.ps1
```

Tests must use synthetic fixtures or resources supplied at runtime by the
developer. Tests must not download or bundle game content.

## Style

- Keep public interfaces documented.
- Prefer small, testable modules.
- Do not add unrelated refactors to bug-fix changes.
- Preserve compatibility with the documented HarmonyOS SDK baseline.
- Report security issues privately using [SECURITY.md](SECURITY.md).
