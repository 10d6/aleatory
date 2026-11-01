# Aleatory — notes for Claude

Aleatory is a synth built on synthkit (the `synthkit/` submodule, pinned).
The owner plays it and gives feedback; Claude writes the code and opens PRs.
The owner tests via the PR's GitHub Pages preview, and installs the plugin zips
from CI at milestones.

## Ground rules

1. **`synthkit/API.md` is the contract.** Work in this repo needs only that
   page plus this repo. Don't read synthkit's source to build the synth; if
   the page is missing something, note it and fix the doc in a synthkit PR.
2. **Don't edit `synthkit/` here.** Kit changes go to the synthkit repo; this
   repo then moves the submodule to the new commit in its own PR.
3. **Real-time safety** (API.md §4a): no allocation, locks, I/O or exceptions
   in `start`, `release`, `render`, `processGlobal`, `processEvents`, `tune`,
   `allocateVoice`. Allocate in `prepare`. No `throw`/`catch` anywhere.
4. **Stable identifiers.** Parameter IDs, `synth.json` `id`, `pluginCode`,
   `vendorCode` and `bundleId` never change once released. Patches store real
   units.
5. **Verify by rendering** before every PR: `--check` passes, and listen to
   (or look at) the renders when the change is audible.
6. Escape hatches and custom state (API.md §4, §5a) are the way to do unusual
   things. A feature moves into the kit when a second synth needs it.

## Fresh workspace

A new session starts without the toolchains. Install them first (Ubuntu; the
same script CI uses):

```
git submodule update --init
synthkit/tools/ci/deps.sh native && synthkit/tools/ci/deps.sh web && synthkit/tools/ci/deps.sh plugin
```

Playwright/Chromium for the browser test: usually preinstalled (try
`NODE_PATH=$(npm root -g)`), else `npm install --no-save playwright@1.48.2 &&
npx playwright install --with-deps chromium`. synthkit's CLAUDE.md has more
(pluginval, a local JUCE checkout).

## About this synth

Notes for whoever works on it next (the README has the musical side):
- `src/Synth.cpp` holds everything. Key presses start per-key patterns in
  `processEvents`; ghost notes are scheduled a pass at a time on MIDI channel
  16 (`kGhostChannel`) and tagged by key, so `ctx.cancel(tag)` stops a pattern.
- `makeMotif` is the original sketch's phrase generator. It takes any uniform
  random source so it can be checked against the sketch's JavaScript (the
  port matched it on 2,016 motifs); keep it faithful when changing it.
- Voices have no kit amp envelope (`ampEnv: false`): the main note uses env1,
  ghosts env2, applied in `render`.
- LFOs are player-added (`standardLfos: { max: 8, start: 1 }`), so code must
  not read a particular LFO (`ctx.lfo(n)`); use the matrix. `max` may only go
  up. `patchVersion` 2 marks that change: `migratePatch` gives v1 patches
  (saved with two fixed LFOs) a second LFO when a route uses `lfo2`
  (`tests/v1-patch-keeps-lfo2.json` checks it).
- Phrase parameters are deliberately not modulatable: they're read when a key
  goes down, and modulating them would make a motif set stop being
  reproducible.
- Info text for every parameter lives in `ui/layout.json`'s `paramInfo`
  (panel text in each section's `info`). Keep it true when behaviour changes.
- Not ported yet: the sketch's A/B button and its spectrum and phrase views.

## Commands

```
cmake -S . -B build -G Ninja && cmake --build build
./build/aleatory_render --check . --out build/renders
./build/aleatory_render tests/hold.json --out x.wav
./build/aleatory_render --params

# browser page (API.md §11)
cmake -S . -B build-web -G Ninja -DCMAKE_TOOLCHAIN_FILE=synthkit/cmake/wasm32-wasi.cmake && cmake --build build-web
node synthkit/web/test/browser-test.cjs build-web/web/aleatory.html build-web/test tests/hold.json

# plugins (API.md §12)
cmake -S . -B build-plugin -G Ninja -DSYNTHKIT_PLUGIN=ON -DSYNTHKIT_WEB_UI_DIR=$PWD/build-web/web && cmake --build build-plugin
```

## CI

Every PR: native checks, the browser page (Chromium test + browser-vs-native
comparison), a Pages preview linked on the PR, and plugin builds for Windows,
macOS and Linux (plugin check, pluginval, auval) with a zip per OS. A `v*` tag
publishes a release. The steps live in `synthkit/tools/ci/`.
