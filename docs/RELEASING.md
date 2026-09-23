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

   Pushing a `v*` tag triggers the build workflow, so CI confirms the tagged
   commit compiles before the release goes out.

8. **Create the GitHub release** from the tag, using the release-notes file as
   the body:

   ```
   gh release create vX.Y.Z --title "vX.Y.Z — <short line>" \
     --notes-file docs/RELEASE-NOTES-vX.Y.Z.md
   ```

   Add `--prerelease` for anything not yet run on real hardware.

## What is deliberately not part of a release

**No binary attachment.** An unsigned executable downloaded from the internet
gets a SmartScreen warning, and shipping one teaches people to click through
SmartScreen warnings. Building from source is also the only way for someone to
be certain the binary matches the code they can read. If that ever changes it
needs a real code-signing certificate, not an exception.

**No installer.** The program is one self-contained file that writes its
settings to `HKCU` and its log to `%LOCALAPPDATA%`. The uninstall instructions
in the README are four commands, and that is the point.
