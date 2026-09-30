# Releasing

The process for cutting a version. Short on purpose: there is no installer and
no signing step. There IS a binary — pushing a `v*` tag builds the full
Tesseract-enabled exe and attaches it, with its SHA-256, to a GitHub Release.

## Versioning

[Semantic Versioning](https://semver.org). The version is written in three
places, and nothing in the BUILD checks that they agree — a mismatch compiles
happily and ships an exe whose file name and its own Properties dialog
disagree.

`scripts/release.sh vX.Y.Z` is the check, and that is the reason to use it
rather than tagging by hand: it refuses to tag until all three match and the
release notes for that version exist. CI compares the tag against `VERSION`
only, so it would not catch the other two.

1. `VERSION` — the canonical file. The exe's FILE NAME is read from here by
   both `CMakeLists.txt` and `build.bat`, so that one cannot drift.
2. `src/resource.h` — the `SNIPTEXT_VERSION_*` macros. `SnipTextProUltra.rc` and the
   startup banner both read these, so the file properties and the binary can
   never disagree with each other.
3. `src/SnipTextProUltra.manifest` — `assemblyIdentity version`, which has to be a
   literal.

Tags are `vMAJOR.MINOR.PATCH`.

## Before 1.1: back out the shakedown switches

Verbose logging is on by default for the 1.x shakedown and should come out
once the app has been run on real hardware for a while. It is commented as
temporary at its definition.

When it goes, update [the README's troubleshooting
section](../README.md#troubleshooting) in the same commit — it currently tells
people verbose is the default.

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

7. **Dry-run the FULL build before tagging.**

   ```
   gh workflow run build.yml --ref main
   gh run watch
   ```

   Then download the `SnipTextProUltra` artifact and **check the exe is
   about 10 MB, not about 500 KB.**

   This is not belt-and-braces. Ordinary pushes build with
   `SNIPTEXT_WITH_TESSERACT` **off**, so a green CI run proves nothing about
   the half of the tag build that resolves vcpkg dependencies, downloads the
   OCR model and embeds it. `workflow_dispatch` runs that entire expensive
   path with `SNIPTEXT_RELEASE=OFF` and the release step gated on tags, so it
   validates everything and publishes nothing.

   The size check is the one assertion the workflow never makes itself. The
   `.rc` embeds the model behind `#ifdef SNIPTEXT_WITH_TESSERACT`, which
   reaches the *resource* compiler only because CMake forwards the target's
   compile definitions onto the RC command line. If that ever stopped
   working, the C++ side would still compile the Tesseract path,
   `FindResource` would return null, and OCR would silently fall back to
   `Windows.Media.Ocr` — nobody would notice until someone captured a serial
   number. A ~500 KB exe is the tell.

   Second payoff: `vcpkg.json` pins no baseline and the vcpkg cache is
   scoped per ref, so a tag build normally gets a cold cache and spends
   twenty minutes rebuilding Tesseract and Leptonica. This run happens on
   `main`, and tag runs *can* restore the default branch's cache — so the
   dry run warms it and the real tag build finishes in minutes.

8. **Run the release script.**

   ```
   ./scripts/release.sh vX.Y.Z
   ```

   It checks that `VERSION`, `src/resource.h`, `src/SnipTextProUltra.manifest` and
   `docs/RELEASE-NOTES-vX.Y.Z.md` all agree with the tag you asked for, then
   commits, pushes, tags and pushes the tag — stopping at the first thing that
   fails.

   > **Why a script rather than four commands.** The sequence is commit →
   > push → tag → push-tag, and a tag points at a *commit*, not at your
   > working tree. If the commit quietly fails — a stale `.git/index.lock` is
   > the classic cause, and it makes `git add` and `git commit` fail while
   > `git tag` still succeeds — you get a tag on the previous commit. CI then
   > builds and publishes the *old* code under the *new* version number. That
   > is exactly what happened to v1.2.0 and v1.3.0, twice, without anybody
   > doing anything wrong.
   >
   > The script clears a stale lock, refuses a tag that already exists, and
   > refuses to tag a dirty tree. CI repeats the version check independently,
   > so a hand-made tag is caught too.

9. **Check the release page.** Confirm the binary is attached and the notes
   rendered.

   If the tag was wrong, delete the release and the tag together and redo
   step 8:

   ```
   gh release delete vX.Y.Z --cleanup-tag --yes
   ```

## The binary, and what it does not come with

Releases attach `SnipTextProUltra_<version>.exe` — the version is part of the file name, read from `VERSION` by both CMake and `build.bat`, so it cannot drift from what the binary reports about itself. It is **not code-signed**, so
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
settings to `HKCU` and writes no log. The uninstall instructions
in the README are four commands, and that is the point.
