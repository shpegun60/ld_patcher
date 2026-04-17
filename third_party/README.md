`third_party/` is kept intentionally small.

Current policy:
- `libzip/`
  - vendored upstream source only

Generated dependency build trees do not live here.
They are written to:
- `../build/<qt-build-dir>/libzip-shared`

That keeps third-party source separate from generated build artifacts.
