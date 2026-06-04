# HTML MOTD NetSurf POC

Goal: keep Android HTML MOTD on `android.webkit.WebView`, and evaluate a
desktop-only NetSurf backend for Linux/Windows MOTD rendering.

## Current State

- Android uses the Java WebView overlay through `Platform_ShowHtmlMotd`.
- Desktop targets now have a non-Android fallback in
  `engine/platform/html_motd.c`, so the engine has a stable entry point while
  the NetSurf backend is developed.
- The fallback returns `0`, which lets the existing HUD MOTD path continue until
  the desktop backend can really render HTML.

## NetSurf Findings

NetSurf is not a single embeddable library. The browser source requires the
NetSurf project libraries before a frontend can be built:

- `buildsystem`
- `libwapcaplet`
- `libparserutils`
- `libhubbub`
- `libcss`
- `libdom`
- `libnsbmp`
- `libnsgif`
- `libnsfb` for the framebuffer frontend

The framebuffer frontend is the closest technical match for Xash because it can
render into memory-backed surfaces. The monkey frontend is useful for tests
because it emits plot commands on stdout.

## License Risk

NetSurf source is GPLv2. Xash sources in this tree are GPLv3-or-later. Directly
linking NetSurf into the engine is therefore a license compatibility risk and
must not be treated as production-safe until the licensing path is confirmed.

For a POC, prefer an external helper process or isolated test tool first:

- Build/run `nsmonkey` or `nsfb` separately.
- Feed it MOTD HTML from a temporary file or local URL.
- Capture either plot commands (`nsmonkey`) or a framebuffer/RGBA buffer
  (`nsfb`/`libnsfb`).
- Convert that into a texture in Xash only after the technical output path is
  proven.

## Preferred POC Path

1. Build NetSurf framebuffer or monkey frontend on Linux first.
2. Load a representative CS 1.6 MOTD HTML file.
3. Confirm it renders:
   - body/pre/a/font/basic CSS
   - images
   - links
   - vertical and horizontal extents for scroll visibility
4. Capture output:
   - framebuffer path: RGBA buffer, width, height, row stride
   - monkey path: plot command stream
5. Add a desktop backend that owns:
   - MOTD HTML state
   - scroll offsets
   - link hit testing
   - close button state
   - texture upload hook
6. Keep Android WebView untouched.

## Production Direction

If the license issue is solved, the cleanest production version is a custom
NetSurf frontend linked into desktop builds only:

- register NetSurf mandatory tables: `misc`, `window`, `fetch`, `bitmap`,
  `layout`
- draw to an offscreen RGBA buffer
- upload that buffer to the renderer
- draw the existing MOTD panel and scroll controls in the game renderer

If the license issue is not solved, keep NetSurf as a separate helper process or
choose a more permissively licensed browser backend for desktop.
