# v1.3.0 — withdrawn

This tag was created before its commit existed, so it pointed at an older
commit: the binary it produced reported itself as **1.1.0**, and its release
body fell back to the entire changelog because the release notes for 1.3.0
were not in that commit either.

The same thing had happened to [v1.2.0](RELEASE-NOTES-v1.2.0.md).

Everything intended for both ships in
[v1.3.1](RELEASE-NOTES-v1.3.1.md).

CI now refuses to build a tag whose `VERSION` file does not match it, or whose
release notes are missing from the tagged commit, so a tag can no longer
quietly describe something other than what it builds. `scripts/release.sh`
does the commit-then-tag sequence in one step for the same reason.
