# Aleatory

A synth built on [synthkit](https://github.com/10d6/synthkit). It runs in the
browser and as a VST3 / AU plugin.

**Every key grows its own melody.** Holding a key plays a sustained FM tone and
starts a pattern of quieter "ghost" notes above (or around) it. Each key has its
own motif: a random walk over the chosen scale that prefers small steps, turns
back after leaps, is pulled toward the key by *Gravity*, and ends on the root,
fifth or octave, with a Euclidean rhythm thinned and sprinkled by *Chaos*. The
pattern repeats, changing a few notes each pass (*Mutations*; with *Drift* the
changes build on each other), and then plays out, stops, or loops while the key
is held (*Pattern mode*). Ghosts go through a band-pass tuned to their own
pitch, with their own envelope, so they sit behind the main note like overtones.

- Steps are 1/16 notes (or the *Rate* setting) **at the DAW's tempo**.
- Motifs come from the *Motif set* number, so a project always reopens with
  the same phrases. *New motifs* moves to the next set.
- Generated notes play on MIDI channel 16; play the synth on any other channel.
- **LFOs:** a patch starts with one; *Add LFO* adds more (up to 8) and × removes
  one. Each preset keeps its own. Route them in *Routes* (or right-click a
  knob → *Modulate…*) to Brightness, Vibrato, the ghosts' Level, Band-pass, Q
  and Legato, and the Echo. The *Tides* preset uses three.
- **MIDI controllers:** CC 74 moves Brightness and CC 71 the ghosts' Band-pass.
- **Tempo in patches:** in the browser, a saved patch keeps its tempo and time
  signature. In a DAW the song's tempo rules.
- The ⓘ next to a control or panel explains what it does.

## Compared with the browser sketch

Ported from the playable sketch in the "Aleatoric synth design" chat. The
phrase generator is the sketch's code, checked note for note against it on
2,016 motifs. Differences:

- Tempo follows the host (the sketch had its own tempo slider), with a *Rate*
  choice for the step length.
- Motifs are reproducible per *Motif set* instead of random each session.
- The main note and ghost loudness follow how hard you play (the sketch
  played the main note at a fixed velocity).
- The main note and the ghosts have separate full envelopes (the sketch shared
  attack and decay).
- Echo time and feedback are adjustable; the A/B button and the spectrum and
  phrase views aren't ported yet.

## Playing it

- **In the browser:** every PR gets a preview link (posted on the PR), and
  `main` is at the repo's GitHub Pages site.
- **In your DAW:** each PR's CI run has a **plugin-windows**, **plugin-macos**
  and **plugin-linux** download (under *Checks → CI → Summary → Artifacts*).
  Unzip it and follow `INSTALL.md` inside. Tagging a version (`v0.2.0`)
  publishes them as a release.

## Building

```
git clone --recursive <this repo>
cmake -S . -B build && cmake --build build
./build/aleatory_render --check . --out renders     # every check; WAVs into renders/
```

The browser page and the plugins are built the same way as in synthkit: see
[`synthkit/API.md`](synthkit/API.md) §11–12.

## Layout

| | |
|---|---|
| `synth.json` | identity (name, IDs, plugin codes) and kit features |
| `src/` | the sound engine |
| `presets/` | factory presets (`init.json` is what a new instance starts on) |
| `tests/` | render scenarios checked in CI |
| `ui/layout.json` | the panel layout (and `ui/custom.js` for custom widgets) |
| `synthkit/` | the kit, as a pinned submodule |
