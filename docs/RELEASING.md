# Releasing

The process for cutting a version. It is short on purpose: there is no
installer, no signing step and no binary to publish, so a release is a tag and
some prose.

## Versioning

[Semantic Versioning](https://semver.org). The version is written in three
places and nothing enforces that they agree:

1. `VERSION` — the canonical file.
2. `src/resource.h` — the `SNIPTEXT_VERSION_*` macros. `SnipText.rc` and the
   startup log line both read these, so the file properties and the log can
   never disagree with each other.
3. `src/SnipText.manifest` — `assemblyIdentity version`, which has to be a
   literal.

Tags are `vMAJOR.MINOR.PATCH`.

## Before 1.1: back out the shakedown switches

Two things are on for the 1.x shakedown and should come out once the app has
actually been run on hardware for a while:

- `kVerboseByDefault` in `src/Log.h` → `false`.
- The `WriteStartupDiagnostics()` call in `App::Run()` → delete it, and the
  function with it.

Both are commented as temporary at their definitions. Nothing else depends on
either. Update [the README's troubleshooting section](../README.md#the-log)
in the same commit — it currently tells people verbose is the default.

## Steps

1. **Update `VERSION`**, and the two files above.

2. **Move the changelog's `[Unreleased]` section into a new version heading**
   with today's date, and add a fresh empty `[Unreleased]`. Update the two
   link definitions at the bottom of `CHANGELOG.md`.

3. **Write `docs/RELEASE-NOTES-vX.Y.Z.md`.** This is the body of the GitHub
   release. The changelog is a terse record for people scanning history; the
   release notes are for someone deciding whether to install it. They should
   lead with whatever they most need to know — for v1.0.0 that was "this has
   never been run on a Windows machine".

4. **Link it** from the Documentation section of `README.md`.

5. **Check the honesty section.** `README.md` has a
   *What has and has not been tested* heading. It is the most important
   section in the project and the easiest one to let drift. If you have run
   the build on real hardware since the last release, say so and say on what.

6. **Run the tests.**

   ```
   build.bat test
   ```

   Or, via CMake:

   ```
   cmake -B build -A x64
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   ```

7. **Commit, tag and push.**

   ```
   git add -A
   git commit -m "Release vX.Y.Z"
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin main --follow-tags
   ```

   That is the whole release. Pushing a `v*` tag runs the build workflow,
   which compiles, runs the tests, computes a SHA-256, and publishes a GitHub
   release with the executable and the checksum attached.

   The release body is `docs/RELEASE-NOTES-vX.Y.Z.md` when that file exists
   and `CHANGELOG.md` when it does not — so a tag pushed before its notes were
   written still gets something useful rather than an empty page.

   **The tests gate the release.** A failing test fails the job before the
   publish step, so a broken tag produces no release at all rather than a
   release nobody should download.

8. **Check the release page.** Confirm the binary is attached and the notes
   rendered.

   If the tag was wrong, delete the release and the tag together and redo
   step 7:

   ```
   gh release delete vX.Y.Z --cleanup-tag --yes
   ```

## The binary, and what it does not come with

Releases attach `SnipTextProUltra.exe`. It is **not code-signed**, so
SmartScreen warns about it, and that warning is accurate — Windows cannot tell
who published it.

The README says so plainly rather than telling people to ignore it, and it
points at building from source as the alternative. Do not soften that wording
in a future release: the instinct to click through a SmartScreen warning is
worth more than the convenience of a download, and a project that trains
people out of it has done them a disservice.

Signing properly needs a real code-signing certificate. That is the fix if
this ever matters enough — not a reassuring paragraph.

The `SHA-256` published beside the binary is **not** a substitute. It proves
the file matches what CI built; it says nothing about who built it or whether
the source was sound.

**No installer.** The program is one self-contained file that writes its
settings to `HKCU` and its log to `%LOCALAPPDATA%`. The uninstall instructions
in the README are four commands, and that is the point.
