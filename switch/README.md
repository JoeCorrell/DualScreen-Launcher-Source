# Embedded Eden DS backend

`eden-upstream` is a source snapshot of Eden DS and remains isolated from the Launcher frontend.
`eden-runtime` adapts that source into an Android library which is packaged into the Launcher APK.
There is no external Eden application, intent hand-off, or second launcher.

The Launcher owns library/content management and initializes Eden lazily for Switch operations.
PC titles continue to use the existing container engine. Switch titles use Eden's native core,
Vulkan renderer, input, saves, content manager, patches, and dual-display presentation.

Eden DS and its inherited emulator code are licensed under GPL-3.0-or-later. See
`eden-upstream/LICENSE.txt` and the source-file SPDX headers.
