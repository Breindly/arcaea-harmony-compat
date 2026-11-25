# Security Policy

## Supported versions

Security fixes target the latest release on the default branch.

## Reporting

Do not open a public issue for vulnerabilities involving path traversal, unsafe
archive extraction, arbitrary native-library loading, secret exposure or memory
corruption.

Send a private report to the maintainers with:

- Affected version and commit
- Reproduction steps
- Expected and observed behavior
- Relevant logs with personal data removed
- Suggested mitigation, when known

## Repository safety rules

- Do not commit save files, account data, device identifiers or crash dumps.
- Do not commit signing keys or credentials.
- Validate archive paths before extraction.
- Reject absolute paths, `..` traversal, symlinks and excessive expansion.

## Scope

This policy covers the compatibility layer source code. It does not authorize
testing against systems or accounts without permission.
