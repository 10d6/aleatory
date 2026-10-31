// Aleatory: every key you press grows its own melody.
//
// A held key plays a sustained FM tone (the "main" note) and starts a pattern
// of quieter "ghost" notes above or around it. Each key has its own motif,
// made from a seeded random walk over a scale, with a Euclidean rhythm. The
// pattern repeats, mutating a few notes each pass, and stops, loops or plays
// out when the key is lifted. Ghosts go through a band-pass filter tuned to
// their own pitch, so they sit behind the main note like overtones.
//
// Musical time comes from the host: a pattern step is a 1/16 note (or the
// Rate setting) at the DAW's tempo.
//
// Ported from the browser sketch in the "Aleatoric synth design" chat, using
// only synthkit/API.md.
#include <synthkit/synth.h>

#include <array>
#include <cmath>

using namespace sk;

namespace {

constexpr int kGhostChannel = 15;  // generated notes play on MIDI channel 16
constexpr int kSteps = 16;         // steps in one pass of a pattern
constexpr int kMaxPool = 28;       // 7-note scale x 4 octaves
constexpr int kMaxNotes = kSteps;
constexpr int kLastNoteSteps = 6;  // the cadence note rings for 6 steps

struct Scale {
  int size;
  int degrees[7];
};
constexpr Scale kScales[] = {
    {5, {0, 2, 4, 7, 9}},           // major pentatonic
    {5, {0, 3, 5, 7, 10}},          // minor pentatonic
    {7, {0, 2, 4, 6, 7, 9, 11}},    // lydian
    {5, {0, 2, 3, 7, 8}},           // hirajoshi
};
constexpr double kRateBeats[] = {0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};  // 1/8, 1/8T, 1/16, 1/16T, 1/32

enum PatternMode { PlayToEnd = 0, StopOnRelease = 1, LoopWhileHeld = 2 };

struct Note {
  int step = 0;    // 0…15 within the pass
  int idx = 0;     // index into the motif's pitch pool
  float vel = 0.5f;
  int dur = 1;     // in steps
  bool mutated = false;
};

struct Motif {
  std::array<int, kMaxPool> pool{};  // semitones from the key
  int poolSize = 0;
  int rootIdx = 0;
  std::array<Note, kMaxNotes> notes{};
  int count = 0;
};

// What a motif was made from; a change means the key gets a new motif.
struct MotifInputs {
  int set = -1, scale = -1;
  bool above = false;
  int chaos = -1, density = -1, gravity = -1;  // in percent, like the original's sliders
  bool operator==(const MotifInputs&) const = default;
};

// The motif for one key, ported from the sketch's makeMotif. Every random
// choice comes from a stream seeded by the motif set and the key, so the same
// settings always give the same phrase (a reopened project sounds the same).
// R is any source of uniform [0, 1) numbers (a dsp::Rng in the synth; the
// sketch's own generator when checking this port against it).
template <typename Uniform>
void makeMotif(int key, const MotifInputs& in, Motif& m, Uniform&& R) {
  const Scale& sc = kScales[in.scale];
  const float chaos = in.chaos / 100.f, dens = in.density / 100.f, grav = in.gravity / 100.f;

  m.poolSize = 0;
  for (int o = in.above ? 0 : -1; o <= 2; ++o)
    for (int s = 0; s < sc.size; ++s) m.pool[static_cast<std::size_t>(m.poolSize++)] = o * 12 + sc.degrees[s];
  m.rootIdx = in.above ? 0 : sc.size;  // where 0 semitones sits
  const int top = m.poolSize - 1;

  // Euclidean rhythm, rotated per key, then thinned and sprinkled by chaos.
  const int k = static_cast<int>(std::lround(3 + dens * 8));
  std::array<int, kSteps> hits{};
  for (int i = 0, b = 0; i < kSteps; ++i) {
    b += k;
    if (b >= kSteps) { b -= kSteps; hits[static_cast<std::size_t>(i)] = 1; }
  }
  const int rot = (key * 5) % kSteps;
  std::array<int, kSteps> on{};
  int nOn = 0;
  on[static_cast<std::size_t>(nOn++)] = 0;
  for (int i = 1; i < kSteps; ++i) {
    const bool hit = hits[static_cast<std::size_t>((i + rot) % kSteps)] != 0;
    if (hit ? R() > chaos * 0.35f : R() < chaos * 0.12f) on[static_cast<std::size_t>(nOn++)] = i;
  }

  // A random walk that prefers small steps, turns back after leaps, and is
  // pulled toward the key by gravity. It reflects off the ends of the range.
  int idx = m.rootIdx, last = 0;
  m.count = 0;
  for (int j = 0; j < nOn; ++j) {
    if (j > 0) {
      const int size = R() < chaos * 0.5f ? 2 + static_cast<int>(R() * 3) : (R() < 0.25f ? 0 : 1);
      int dir;
      if (std::abs(last) >= 2) {
        dir = last > 0 ? -1 : 1;
      } else {
        const float dist = static_cast<float>(idx - m.rootIdx);
        const float pd = 0.5f + dsp::clamp(dist * grav * 0.12f, -0.45f, 0.45f);
        dir = R() < pd ? -1 : 1;
      }
      int ni = idx + size * dir;
      if (ni < 0) ni = -ni;
      if (ni > top) ni = 2 * top - ni;
      ni = std::max(0, std::min(top, ni));
      last = ni - idx;
      idx = ni;
    }
    const float arc = std::sin(dsp::kPi * (static_cast<float>(j) + 0.5f) / static_cast<float>(nOn));
    m.notes[static_cast<std::size_t>(m.count++)] = Note{on[static_cast<std::size_t>(j)], idx, 0.3f + arc * 0.4f, 1, false};
  }

  // End on a resting note: the root, the fifth or the octave.
  if (m.count > 1) {
    Note& L = m.notes[static_cast<std::size_t>(m.count - 1)];
    int best = m.rootIdx;
    static constexpr int kAbove[] = {0, 7, 12}, kAround[] = {-12, -5, 0, 7, 12};
    const int* targets = in.above ? kAbove : kAround;
    const int nTargets = in.above ? 3 : 5;
    for (int t = 0; t < nTargets; ++t)
      for (int i = 0; i < m.poolSize; ++i)
        if (m.pool[static_cast<std::size_t>(i)] == targets[t]) {
          if (std::abs(i - L.idx) < std::abs(best - L.idx)) best = i;
          break;
        }
    L.idx = best;
  }
  for (int i = 0; i < m.count; ++i) {
    auto& n = m.notes[static_cast<std::size_t>(i)];
    n.dur = i < m.count - 1 ? m.notes[static_cast<std::size_t>(i + 1)].step - n.step : kLastNoteSteps;
  }
}

// Nudge a few inner notes by a scale step or two (never the first or last).
void mutate(const Note* src, int count, int changes, int poolSize, dsp::Rng& rng, Note* out) {
  std::array<int, kMaxNotes> cand{};
  int nCand = 0;
  for (int i = 0; i < count; ++i) {
    out[i] = src[i];
    out[i].mutated = false;
    if (i > 0 && i < count - 1) cand[static_cast<std::size_t>(nCand++)] = i;
  }
  for (int m = 0; m < changes && nCand > 0; ++m) {
    const int pick = std::min(nCand - 1, static_cast<int>(rng.uniform() * static_cast<float>(nCand)));
    const int i = cand[static_cast<std::size_t>(pick)];
    cand[static_cast<std::size_t>(pick)] = cand[static_cast<std::size_t>(--nCand)];
    const int s = (rng.uniform() < 0.7f ? 1 : 2) * (rng.uniform() < 0.5f ? -1 : 1);
    int ni = out[i].idx + s;
    if (ni < 0 || ni >= poolSize) ni = out[i].idx - s;
    out[i].idx = std::max(0, std::min(poolSize - 1, ni));
    out[i].mutated = true;
  }
}

// A band-pass biquad with 0 dB at the centre (the classic "constant peak"
// design, as in Web Audio's BiquadFilter), so ghosts keep their level.
struct BandPass {
  float b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(float hz, float q, float sr) {
    const float w = dsp::kTwoPi * dsp::clamp(hz, 10.f, sr * 0.45f) / sr;
    const float alpha = std::sin(w) / (2.f * q);
    const float a0 = 1.f + alpha;
    b0 = alpha / a0;
    a1 = -2.f * std::cos(w) / a0;
    a2 = (1.f - alpha) / a0;
  }
  float process(float x) {  // transposed direct form II; b1 = 0, b2 = -b0
    const float y = b0 * x + z1;
    z1 = -a1 * y + z2;
    z2 = -b0 * x - a2 * y;
    z1 = dsp::flushDenormal(z1);
    z2 = dsp::flushDenormal(z2);
    return y;
  }
  void reset() { z1 = z2 = 0; }
};

// A feed-forward compressor with a soft knee and automatic make-up gain,
// standing in for the sketch's Web Audio DynamicsCompressor.
struct Compressor {
  float thresholdDb = -16, ratio = 6, kneeDb = 30;
  float attackCoef = 0, releaseCoef = 0, envDb = -120, makeup = 1;
  void prepare(float sr) {
    attackCoef = std::exp(-1.f / (0.005f * sr));
    releaseCoef = std::exp(-1.f / (0.25f * sr));
    makeup = std::pow(1.f / dsp::dbToGain(curve(0.f)), 0.6f);  // Web Audio's make-up rule
  }
  float curve(float xDb) const {  // output level for an input level, in dB
    const float lo = thresholdDb - kneeDb / 2, hi = thresholdDb + kneeDb / 2;
    if (xDb <= lo) return xDb;
    if (xDb >= hi) return thresholdDb + (xDb - thresholdDb) / ratio;
    const float d = xDb - lo;
    return xDb + (1.f / ratio - 1.f) * d * d / (2.f * kneeDb);
  }
  float process(float x) {
    const float inDb = dsp::gainToDb(std::fabs(x));
    const float c = inDb > envDb ? attackCoef : releaseCoef;
    envDb = inDb + c * (envDb - inDb);
    return x * dsp::dbToGain(curve(envDb) - envDb) * makeup;
  }
};

}  // namespace

struct Aleatory;

struct AleaVoice : Voice {
  explicit AleaVoice(Aleatory& s) : synth(s) {}
  void prepare(const PrepareInfo& info) override { sr = static_cast<float>(info.sampleRate); }
  void start(const NoteOn& n, const StartInfo& s) override;
  void render(VoiceContext& ctx, Buffer& out) override;

  Aleatory& synth;
  float sr = 48000;
  bool ghost = false;
  float vel = 0.5f;
  double carPhase = 0, modPhase = 0, vibPhase = 0;
  float vibRate = 4.8f;
  double t = 0;          // seconds since the note started
  double tRelease = -1;  // when the key went up
  float idxAtRelease = 0, lastIdx = 0;
  float bpHz = 0;
  BandPass bp[2];
};

struct Aleatory : Synth {
  // Pattern
  Param mode, repeats, rate;
  // Phrase
  Param scale, chaos, density, gravity, mutations, above, drift, motifSet;
  // Voice
  Param brightness, vibrato;
  // Ghosts
  Param ghostLevel, ghostFilter, bpAmount, bpQ, bpStages, legato, ghostsOnly;
  // Echo
  Param echoMix, echoTime, echoFeedback;
  // Kit envelopes, read to shape the FM index like the amplitude (index 1 = main, 2 = ghosts)
  Param envA[3], envD[3], envR[3];

  void defineParams(ParamBuilder& b) override {
    mode = b.add("pattern.mode").name("Pattern Mode").choices({"Play to end", "Stop on release", "Loop while held"}).def(StopOnRelease).group("Pattern");
    repeats = b.add("pattern.repeats").name("Repeats").range(1, 6).integer().def(3).group("Pattern");
    rate = b.add("pattern.rate").name("Rate").choices({"1/8", "1/8T", "1/16", "1/16T", "1/32"}).def(2).group("Pattern");

    scale = b.add("phrase.scale").name("Scale").choices({"Major pentatonic", "Minor pentatonic", "Lydian", "Hirajoshi"}).group("Phrase");
    chaos = b.add("phrase.chaos").name("Chaos").range(0, 100).integer().def(30).group("Phrase");
    density = b.add("phrase.density").name("Density").range(0, 100).integer().def(35).group("Phrase");
    gravity = b.add("phrase.gravity").name("Gravity").range(0, 100).integer().def(40).group("Phrase");
    mutations = b.add("phrase.mutations").name("Mutations").range(0, 3).integer().def(1).group("Phrase");
    above = b.add("phrase.above").name("Ghosts At or Above Key").toggle().def(1).group("Phrase");
    drift = b.add("phrase.drift").name("Drift").toggle().def(0).group("Phrase");
    motifSet = b.add("phrase.set").name("Motif Set").range(1, 999).integer().def(1).group("Phrase");

    brightness = b.add("voice.brightness").name("Brightness").range(0.4, 4).def(2.2).modulatable().group("Voice");
    vibrato = b.add("voice.vibrato").name("Vibrato").range(0, 30).def(8).unit("ct").modulatable().group("Voice");

    ghostLevel = b.add("ghost.level").name("Ghost Level").range(0, 1).def(0.75).modulatable().group("Ghosts");
    ghostFilter = b.add("ghost.filter").name("Ghost Filter").toggle().def(1).group("Ghosts");
    bpAmount = b.add("ghost.bp").name("Band-pass Amount").range(0, 1).def(0.6).modulatable().group("Ghosts");
    bpQ = b.add("ghost.q").name("Band-pass Q").range(0.5, 8).log().def(2).modulatable().group("Ghosts");
    bpStages = b.add("ghost.stages").name("Band-pass Stages").choices({"1 (gentle)", "2 (steeper)"}).group("Ghosts");
    legato = b.add("ghost.legato").name("Ghost Legato").range(0.2, 1).def(0.85).group("Ghosts");
    ghostsOnly = b.add("ghost.only").name("Ghosts Only").toggle().def(0).group("Ghosts");

    echoMix = b.add("echo.mix").name("Echo").range(0, 1).def(0.22).modulatable().group("Echo");
    echoTime = b.add("echo.time").name("Echo Time").range(0.05, 1).log().def(0.37).unit("s").group("Echo");
    echoFeedback = b.add("echo.feedback").name("Echo Feedback").range(0, 0.9).def(0.35).group("Echo");

    // env1 shapes the main note, env2 the ghosts. Velocity sets their level
    // in the voice, so the envelopes themselves ignore it.
    b.kitDefault("env1.attack", 0.08);
    b.kitDefault("env1.decay", 0.5);
    b.kitDefault("env1.sustain", 0.65);
    b.kitDefault("env1.release", 1.5);
    b.kitDefault("env1.velocity", 0);
    b.kitDefault("env2.attack", 0.25);
    b.kitDefault("env2.decay", 0.5);
    b.kitDefault("env2.sustain", 0.5);
    b.kitDefault("env2.release", 1.2);
    b.kitDefault("env2.velocity", 0);
  }

  std::unique_ptr<Voice> makeVoice() override { return std::make_unique<AleaVoice>(*this); }

  // ---- patterns ---------------------------------------------------------------

  struct Pattern {
    bool running = false;
    int key = 0;
    double nextBeat = 0;  // where the next pass starts
    int pass = 0, reps = 3, mode = StopOnRelease;
    bool firstPlay = false, ghostsOnly = false;
    float velScale = 1;
    std::array<Note, kMaxNotes> prev{};  // the last pass, for Drift
    int prevCount = 0;
  };

  std::array<Motif, 128> motifs{};
  std::array<MotifInputs, 128> motifInputs{};
  std::array<bool, 128> played{};                          // has this motif been heard yet?
  std::array<std::array<Note, kMaxNotes>, 128> current{};  // each key's latest pass (drift memory)
  std::array<Pattern, 128> patterns{};
  dsp::Rng rng{0xA1EA7};  // mutations and timing jitter
  double lastBlockEnd = 0;

  void prepare(const PrepareInfo& info) override {
    for (int n = 1; n <= 2; ++n) {
      const std::string p = "env" + std::to_string(n) + ".";
      envA[n] = info.param(p + "attack");
      envD[n] = info.param(p + "decay");
      envR[n] = info.param(p + "release");
    }
    delay.prepare(static_cast<float>(info.sampleRate), 1.1f);
    tone.prepare(static_cast<float>(info.sampleRate));
    tone.set(2400);
    delayTime.prepare(static_cast<float>(info.sampleRate), 80);
    comp.prepare(static_cast<float>(info.sampleRate));
  }

  static uint32_t tagFor(int key) { return static_cast<uint32_t>(key) + 1; }

  void stopPattern(EventContext& ctx, int key) {
    auto& p = patterns[static_cast<std::size_t>(key)];
    if (!p.running) return;
    p.running = false;
    ctx.cancel(tagFor(key));  // unplayed ghosts are dropped; sounding ones release
  }

  void startPattern(EventContext& ctx, const MidiEvent& e) {
    const int key = e.note;
    stopPattern(ctx, key);
    MotifInputs in;
    in.set = static_cast<int>(std::lround(ctx.get(motifSet)));
    in.scale = ctx.choice(scale);
    in.above = ctx.on(above);
    in.chaos = static_cast<int>(std::lround(ctx.get(chaos)));
    in.density = static_cast<int>(std::lround(ctx.get(density)));
    in.gravity = static_cast<int>(std::lround(ctx.get(gravity)));
    auto k = static_cast<std::size_t>(key);
    if (!(motifInputs[k] == in)) {
      dsp::Rng R(dsp::hashSeed(static_cast<uint64_t>(in.set), static_cast<uint64_t>(key) * 7919u,
                               static_cast<uint64_t>(in.scale * 2 + (in.above ? 1 : 0))));
      makeMotif(key, in, motifs[k], [&R] { return R.uniform(); });
      motifInputs[k] = in;
      played[k] = false;
      for (int i = 0; i < motifs[k].count; ++i) current[k][static_cast<std::size_t>(i)] = motifs[k].notes[static_cast<std::size_t>(i)];
    }
    auto& p = patterns[k];
    p.running = true;
    p.key = key;
    p.nextBeat = e.beat;
    p.pass = 0;
    p.reps = static_cast<int>(std::lround(ctx.get(repeats)));
    p.mode = ctx.choice(mode);
    p.firstPlay = !played[k];
    p.ghostsOnly = ctx.on(ghostsOnly);
    // Ghost loudness follows how hard the key was played (the sketch's fixed
    // key velocity was 0.6).
    p.velScale = dsp::clamp(e.vel / 0.6f, 0.3f, 1.6f);
    p.prev = current[k];
    p.prevCount = motifs[k].count;
  }

  // One pass of a pattern: schedule its ghost notes in musical time.
  void schedulePass(EventContext& ctx, Pattern& p) {
    const auto k = static_cast<std::size_t>(p.key);
    const Motif& m = motifs[k];
    const double step = kRateBeats[ctx.choice(rate)];
    const float leg = ctx.get(legato);
    std::array<Note, kMaxNotes> notes{};
    if (p.pass == 0 && p.firstPlay) {
      for (int i = 0; i < m.count; ++i) notes[static_cast<std::size_t>(i)] = m.notes[static_cast<std::size_t>(i)];
    } else {
      const Note* src = ctx.on(drift) ? p.prev.data() : m.notes.data();
      mutate(src, m.count, static_cast<int>(std::lround(ctx.get(mutations))), m.poolSize, rng, notes.data());
    }
    p.prev = notes;
    p.prevCount = m.count;
    current[k] = notes;
    played[k] = true;

    for (int i = 0; i < m.count; ++i) {
      const Note& n = notes[static_cast<std::size_t>(i)];
      // The key itself plays the first note of the first pass.
      if (p.pass == 0 && n.step == 0 && !p.ghostsOnly) continue;
      const int pitch = p.key + m.pool[static_cast<std::size_t>(n.idx)];
      if (pitch < 0 || pitch > 127) continue;
      const double jitter = n.step == 0 ? 0.0 : ctx.seconds((rng.uniform() - 0.5) * 0.03);
      const double at = std::max(ctx.now(), p.nextBeat + n.step * step + jitter);
      const float v = dsp::clamp(std::max(0.15f, n.vel + (rng.uniform() - 0.5f) * 0.12f) * p.velScale, 0.05f, 1.f);
      const double gate = std::max(ctx.seconds(0.05), n.dur * step * leg);
      ctx.schedule(at, NoteOn{pitch, v, kGhostChannel}, tagFor(p.key));
      ctx.schedule(at + gate, NoteOff{pitch, kGhostChannel}, tagFor(p.key));
    }
    p.nextBeat += kSteps * step;
    ++p.pass;
  }

  void processEvents(EventContext& ctx) override {
    for (auto& e : ctx.in()) {
      const bool fromKeys = e.channel != kGhostChannel;
      if (fromKeys && (e.isCC(120) || e.isCC(123))) {  // all sound / notes off
        for (int key = 0; key < 128; ++key) stopPattern(ctx, key);
      }
      if (fromKeys && e.isNoteOn()) {
        startPattern(ctx, e);
        if (!ctx.on(ghostsOnly)) ctx.out(e);
        continue;
      }
      if (fromKeys && e.isNoteOff()) {
        auto& p = patterns[static_cast<std::size_t>(e.note)];
        if (p.mode != PlayToEnd) stopPattern(ctx, e.note);
      }
      ctx.out(e);
    }
    // Schedule each running pattern a pass at a time, a little ahead, so
    // tempo and setting changes apply from the next pass.
    const double horizon = ctx.blockEnd() + ctx.seconds(0.25);
    for (auto& p : patterns) {
      while (p.running && p.nextBeat < horizon) {
        if (p.mode != LoopWhileHeld && p.pass >= p.reps) { p.running = false; break; }
        schedulePass(ctx, p);
      }
    }
    lastBlockEnd = ctx.blockEnd();
  }

  // The host jumped (loop, scrub, new start point): the kit drops what was
  // scheduled, and running patterns carry on from here.
  void onTransportJump(EventContext& ctx) override {
    for (auto& p : patterns)
      if (p.running) p.nextBeat = ctx.now();
    Synth::onTransportJump(ctx);
  }

  // ---- echo and compressor (after the voices) ------------------------------------

  dsp::Delay delay;
  dsp::OnePole tone;
  dsp::Smoother delayTime;
  Compressor comp;

  void processGlobal(GlobalContext& ctx, Buffer& io) override {
    delayTime.setTarget(ctx.get(echoTime));
    const auto mix = ctx.ramp(echoMix);
    const float fb = ctx.get(echoFeedback);
    for (int i = 0; i < io.frames; ++i) {
      const float dry = 0.5f * (io.left[i] + io.right[i]);
      const float wet = tone.lowpass(delay.readSeconds(delayTime.next()));
      delay.write(dry * mix.at(i) + wet * fb);
      const float y = comp.process(dry + wet) * 0.8f;
      io.left[i] = y;
      io.right[i] = y;
    }
  }
};

// ---- the voice -----------------------------------------------------------------

void AleaVoice::start(const NoteOn& n, const StartInfo& s) {
  ghost = n.channel == kGhostChannel;
  vel = n.vel;
  if (!s.sounding) {
    carPhase = modPhase = 0;
    bp[0].reset();
    bp[1].reset();
  }
  vibPhase = 0;
  vibRate = rng().uniform(4.4f, 5.3f);
  t = 0;
  tRelease = -1;
  bpHz = 0;
}

void AleaVoice::render(VoiceContext& ctx, Buffer& out) {
  const int e = ghost ? 2 : 1;
  const float* env = ctx.envSamples(e);
  const float f = static_cast<float>(ctx.pitchHz());
  const float A = std::max(0.001f, ctx.get(synth.envA[e]));
  const float tauD = std::max(0.01f, ctx.get(synth.envD[e]) / 3.f);
  const float R = std::max(0.01f, ctx.get(synth.envR[e]));

  // FM index (in Hz of deviation): rises over the attack, settles with the
  // decay, and falls to a fifth of its peak on release, like the sketch.
  const float pI = ctx.get(synth.brightness) * f * (0.5f + vel * 0.6f) * (ghost ? 0.85f : 1.f);
  const float i0 = pI * (ghost ? 0.3f : 0.15f), iS = pI * 0.55f;
  if (ctx.released() && tRelease < 0) { tRelease = t; idxAtRelease = lastIdx; }

  // Level: the ghosts' level, plus a little for the band-pass's loss.
  const bool filter = ghost && ctx.on(synth.ghostFilter);
  const float amt = filter ? ctx.get(synth.bpAmount) : 0.f;
  float peak = vel * 0.17f;
  if (ghost) peak *= ctx.get(synth.ghostLevel) * (1.f + amt * 0.3f);
  const int stages = filter ? ctx.choice(synth.bpStages) + 1 : 0;
  if (filter) {
    if (bpHz <= 0) bpHz = f;  // tuned to the note, not following vibrato
    for (int s = 0; s < stages; ++s) bp[s].set(bpHz, ctx.get(synth.bpQ), sr);
  }

  const float vib = ctx.get(synth.vibrato);
  const double dt = 1.0 / sr;
  for (int i = 0; i < out.frames; ++i) {
    float idx;
    if (tRelease >= 0) idx = pI * 0.2f + (idxAtRelease - pI * 0.2f) * std::exp(-static_cast<float>(t - tRelease) / (R / 3.f));
    else if (t < A) idx = i0 + (pI - i0) * static_cast<float>(t) / A;
    else idx = iS + (pI - iS) * std::exp(-static_cast<float>(t - A) / tauD);
    lastIdx = idx;

    // Vibrato fades in over the attack plus 0.6 s.
    const float depth = vib * std::min(1.f, static_cast<float>(t) / (A + 0.6f));
    const float detune = std::exp2(depth * std::sin(dsp::kTwoPi * static_cast<float>(vibPhase)) / 1200.f);
    const double fd = f * detune;
    const float m = std::sin(dsp::kTwoPi * static_cast<float>(modPhase));
    const float y0 = std::sin(dsp::kTwoPi * static_cast<float>(carPhase));
    modPhase += fd * dt;
    carPhase += (fd + idx * m) * dt;
    modPhase -= std::floor(modPhase);
    carPhase -= std::floor(carPhase);
    vibPhase += vibRate * dt;
    vibPhase -= std::floor(vibPhase);

    float y = y0;
    if (filter) {
      float w = y0;
      for (int s = 0; s < stages; ++s) w = bp[s].process(w);
      y = (1.f - amt) * y0 + amt * w;
    }
    out.add(i, y * peak * env[i]);
    t += dt;
  }
}

SK_SYNTH(Aleatory)
