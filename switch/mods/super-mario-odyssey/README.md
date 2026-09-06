# Super Mario Odyssey development profile

This enables import/launch through embedded Eden and the development read-only
companion shipped inside the launcher APK: live status, current kingdom map,
touch pan/zoom and a discovery-filtered Moon list. It is **not a completed
dual-screen mod**. The native/UI implementation still requires on-device
acceptance. Travel, wardrobe commands and Mario's live preview are not ready.
No guest payload or save mutation is installed by this manifest.

Initial investigation target: title `0100000000010000`, base control version
`1.0.0`, main build `3CA12DFAAF9C82DA064D1698DF79CDA1` (zero-padded to 32 bytes).
The user's separate 1.4.1 update is a different executable and is not covered by
base-build contracts. Do not silently reuse offsets across versions.

Native evidence and design: `../../eden-upstream/docs/SmoDualScreen.md`.
