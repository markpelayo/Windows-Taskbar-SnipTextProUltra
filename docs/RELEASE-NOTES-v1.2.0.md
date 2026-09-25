# v1.2.0 — withdrawn

This tag was created before its version bump was committed, so the build it
produced reported itself as **1.1.0** and its release body fell back to the
entire changelog. No working 1.2.0 binary was ever published.

Its changes — the clearer command names, the removal of the capture-section
headers, and the *Show Saved Files* submenu — ship in
[v1.3.0](RELEASE-NOTES-v1.3.0.md) instead.

CI now refuses to build a tag whose `VERSION` file does not match it, and
refuses one whose release notes are missing, so this cannot happen quietly
again.
