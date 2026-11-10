# TinyHand security audit

## Scope and verdict

Audited upstream `nilbuild/tinyhand` and this fork at `c5fb36b7c4d7c87480958872a724b30a52427a47`, plus the unmerged Windows contribution at `c5106e9b02630b64a501be4fe5052ef0008a9dd7` before reuse.

No confirmed malware, trojan, RAT, command-and-control, credential theft, hidden downloader, obfuscated payload, or covert persistence was found in the reviewed source. This is a bounded source audit, not a guarantee that every dependency or binary is safe.

## Evidence

- The fork and upstream `main` initially resolved to the same commit and Git tree.
- Sparkle 2.10.0 matched the official pinned revision. Retrieved advisories affected earlier versions.
- The upstream macOS v1.0.2 DMG matched GitHub's published digest and sidecar checksum. Its Sparkle Ed25519 signature verified. CI linked the release to commit `f243657fcbbd0613e004908d188042c14dbb53a5`; this does not prove reproducible source-to-binary equivalence.
- The native app has no browser-cookie, password, Keychain, or credential-file collection. Networking was limited to the declared updater and user-clicked links.
- Windows persistence is limited to the visible, per-user Run-key preference.

## Upstream findings

- Medium: release CI used mutable action/tool references and downloaded Sparkle tools without enforcing a digest while signing credentials were available.
- Low: production included local input-injection test entry points.
- Low: the Mac app enabled login startup on its first installed run rather than asking first.
- Low: mouse coordinates were logged without rotation.
- Low: imported images lacked decoded-size limits.
- Low: custom-hand IDs needed stricter path validation.

## Windows remediation

The Windows port separates test and production executables, makes startup opt-in, caps compressed and decoded image sizes, validates exact custom IDs and storage paths, rotates logs without mouse coordinates, joins thumbnail workers before GDI+ shutdown, and verifies signed update metadata plus package size, SHA-256, and ZIP members.

Input fixes cover left-only drawing, modifier-free forwarded clicks, hook failure, and modifier restoration. Settings handles DPI changes and stale startup entries.

## Validation and remaining limits

- Native x64 production and updater executables compile with MinGW without warnings in the final integration build.
- Shape recognizer tests passed.
- Production does not contain the interactive test switches; PE imports were checked against a native-library allowlist.
- The release ZIP contains 40 expected files. Its OpenSSL ECDSA manifest signature verified.
- The executable is not Authenticode-signed, so Windows SmartScreen may warn.
- The original audit was static/read-only. It did not prove the prebuilt macOS DMG equivalent to source.
- Not every Windows UI/E2E check passed during development; the full interactive suite was not completed before release. Mixed-DPI/multi-monitor behavior, live update installation, and every custom-image edge case still need broader testing.
