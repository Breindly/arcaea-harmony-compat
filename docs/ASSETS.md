# Asset and Legal Boundary

## What may be committed

- Original source code
- Synthetic charts and generated test data
- Publicly redistributable sounds and images with clear licenses
- Empty schemas, placeholders and import instructions
- Checksums of user-provided files when they do not expose private data

## What must remain local

- Game installations and extracted payloads
- Music, charts, artwork, characters, fonts and models
- Converted libraries and audio ZIP files
- Save data and runtime preferences
- Native binaries from the game or platform vendors

## Import model

The compatibility layer should consume resources only from a path selected by
the user at runtime. Import tools must:

1. Require an explicit input path.
2. Write generated output inside the repository or user-selected output path.
3. Reject path traversal and unsupported archive entries.
4. Avoid uploading or logging file contents.
5. Keep generated content ignored by Git.

## Tests

Unit tests must use synthetic fixtures. Integration tests may accept a local
fixture path through an environment variable or command-line argument, but the
path and fixture content must not be committed.
