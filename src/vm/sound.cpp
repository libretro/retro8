#include "sound.h"

#include "memory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <type_traits>

#if SOUND_ENABLED

using namespace retro8;
using namespace retro8::sfx;

namespace
{
  constexpr int32_t LOWEST_KEY = -48;
  constexpr int32_t HIGHEST_KEY = 127;

  /* PICO-8 tunes pitch 33 to A 440 Hz, so pitch 0 is C at 65.4 Hz */
  struct KeyTable
  {
    float hz[HIGHEST_KEY - LOWEST_KEY + 1];

    KeyTable()
    {
      for (int32_t key = LOWEST_KEY; key <= HIGHEST_KEY; ++key)
        hz[key - LOWEST_KEY] = 440.0f * std::pow(2.0f, (key - 33) / 12.0f);
    }

    float operator()(int32_t key) const { return hz[std::min(std::max(key, LOWEST_KEY), HIGHEST_KEY) - LOWEST_KEY]; }
  };

  const KeyTable keyHz;

  inline float fraction(double value) { return float(value - std::floor(value)); }
  inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

  inline float tilted(float t, float a) { return t < a ? 2.0f * t / a - 1.0f : 2.0f * (1.0f - t) / (1.0f - a) - 1.0f; }

  /* the waveforms follow measurements of PICO-8 output, t is the position within one period, and the buzz shapes are
     zepto8's, measured from PICO-8 WAV exports */
  float wave(Waveform waveform, float t, double phase, bool buzz)
  {
    switch (waveform)
    {
    case Waveform::TRIANGLE:
    {
      const float out = std::fabs(4.0f * t - 2.0f) - 1.0f;
      return 0.5f * (buzz ? 0.75f * out - 0.25f * tilted(t, 0.875f) : out);
    }
    case Waveform::TILTED_SAW:
      return 0.5f * tilted(t, buzz ? 0.975f : 0.875f);
    case Waveform::SAW:
    {
      const float out = t < 0.5f ? t : t - 1.0f;
      if (!buzz)
        return 0.653f * out;
      return 0.653f * (0.83f * out - (std::fabs(fraction(phase / 2.0) * 2.0f - 1.0f) < 0.5f ? 0.085f : 0.0f));
    }
    case Waveform::SQUARE:
      return t < (buzz ? 0.4f : 0.5f) ? 0.25f : -0.25f;
    case Waveform::PULSE:
      return t < (buzz ? 0.255f : 1.0f / 3.0f) ? 0.25f : -0.25f;
    case Waveform::ORGAN:
    {
      float out = t < 0.5f ? 3.0f - std::fabs(24.0f * t - 6.0f) : 1.0f - std::fabs(16.0f * t - 12.0f);
      if (buzz)
      {
        if (t < 0.5f)
          out = out * 2.0f + 3.0f;
        out = t < 0.5f && out > -1.875f ? out * 0.2f - 1.0f : out + 0.5f;
      }
      return out / 9.0f;
    }
    case Waveform::PHASER:
    {
      /* two triangles drift against each other at 1/128 of the note frequency */
      const float k = std::fabs(2.0f * fraction(phase / 128.0) - 1.0f);
      const float u = fraction(t + 0.5f * k);
      float out = std::fabs(4.0f * u - 2.0f) - std::fabs(4.0f * t - 2.0f);
      if (buzz)
        out += 0.25f - std::fabs(fraction(phase * 2.0 + 0.5) - 0.5f) + 0.125f - std::fabs(0.5f * fraction(phase * 4.0) - 0.25f);
      return out / 6.0f;
    }
    default:
      return 0.0f;
    }
  }

  /* the second wave detune adds, as a multiple of the note frequency, from zepto8's measurements */
  float detuneRatio(Waveform waveform, int32_t detune)
  {
    switch (waveform)
    {
    case Waveform::TRIANGLE:
      return detune == 1 ? 3.0f / 4.0f : 3.0f / 2.0f;
    case Waveform::ORGAN:
      return detune == 1 ? 200.0f / 199.0f : 800.0f / 199.0f;
    case Waveform::PHASER:
      return detune == 1 ? 49.0f / 50.0f : 400.0f / 199.0f;
    default:
      return detune == 1 ? 200.0f / 199.0f : 400.0f / 199.0f;
    }
  }

  /* dampen is a high shelf, zepto8's 2400 Hz at -6 dB for level 1 and 1000 Hz at -12 dB for level 2 (audio EQ cookbook) */
  struct ShelfCoefficients
  {
    float b0, b1, b2, a1, a2;

    ShelfCoefficients(float frequency, float gain)
    {
      const float w0 = 2.0f * float(M_PI) * frequency / APU::SAMPLE_RATE;
      const float cosw0 = std::cos(w0);
      const float a = std::pow(10.0f, gain / 40.0f);
      const float alpha = std::sin(w0) / 2.0f * std::sqrt(2.0f);
      const float sqra = 2.0f * std::sqrt(a) * alpha;
      const float a0 = (a + 1.0f) - (a - 1.0f) * cosw0 + sqra;
      b0 = a * ((a + 1.0f) + (a - 1.0f) * cosw0 + sqra) / a0;
      b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosw0) / a0;
      b2 = a * ((a + 1.0f) + (a - 1.0f) * cosw0 - sqra) / a0;
      a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosw0) / a0;
      a2 = ((a + 1.0f) - (a - 1.0f) * cosw0 - sqra) / a0;
    }

    float run(Shelf& s, float in) const
    {
      const float out = b0 * in + b1 * s.x1 + b2 * s.x2 - a1 * s.y1 - a2 * s.y2;
      s.x2 = s.x1;
      s.x1 = in;
      s.y2 = s.y1;
      s.y1 = out;
      return out;
    }
  };

  const ShelfCoefficients dampLight(2400.0f, -6.0f);
  const ShelfCoefficients dampStrong(1000.0f, -12.0f);
}

void Voice::start(sound_index_t sound, int32_t first, int32_t notes)
{
  index = sound;
  note = -1;
  limit = notes;
  offset = first;
  played = 0.0;
  clock = 0;
  released = false;
  fade = 0;
  crossfade = 0;
  lastKey = NO_KEY;
  lastVolume = 0.0f;
}

/* PICO-8 mixes at 22050 Hz, so its output stops just under 11 kHz. Measured against PICO-8 on a device it is flat to 9 kHz
   and almost silent above 11 kHz. A windowed sinc gives the same edge, which takes off the harsh top the waveforms
   otherwise have at 44100 Hz. */
void APU::initLowpass()
{
  constexpr double cutoff = 10000.0 / SAMPLE_RATE;
  constexpr double beta = 8.0;
  const auto bessel = [](double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k)
    {
      term *= (x / (2.0 * k)) * (x / (2.0 * k));
      sum += term;
    }
    return sum;
  };

  const double middle = (LOWPASS_TAPS - 1) / 2.0;
  double total = 0.0;
  std::array<double, LOWPASS_TAPS> taps;
  for (size_t i = 0; i < LOWPASS_TAPS; ++i)
  {
    const double n = i - middle;
    const double sinc = n == 0.0 ? 2.0 * cutoff : std::sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
    const double r = n / middle;
    const double window = bessel(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel(beta);
    taps[i] = sinc * window;
    total += taps[i];
  }

  for (size_t i = 0; i < LOWPASS_TAPS; ++i)
    lowpassTaps[i] = float(taps[i] / total);

  lowpassHistory.fill(0.0f);
  lowpassAt = 0;
}

float APU::lowpass(float sample)
{
  lowpassHistory[lowpassAt] = sample;

  float out = 0.0f;
  size_t at = lowpassAt;
  for (size_t i = 0; i < LOWPASS_TAPS; ++i)
  {
    out += lowpassTaps[i] * lowpassHistory[at];
    at = at == 0 ? LOWPASS_TAPS - 1 : at - 1;
  }

  lowpassAt = (lowpassAt + 1) % LOWPASS_TAPS;
  return out;
}

void APU::init()
{
  static_assert(sizeof(SoundSample) == 2, "Must be 2 bytes");
  static_assert(sizeof(Sound) == 68, "Must be 68 bytes");

  for (size_t i = 0; i < CHANNEL_COUNT; ++i)
  {
    sfxChannels[i] = Channel();
    musicChannels[i] = Channel();
  }

  initLowpass();

  pattern = -1;
  musicVolume = 1.0f;
  musicFade = 0.0f;
  musicStopping = false;
}

void APU::play(sound_index_t index, channel_index_t channel, int32_t start, int32_t length)
{
  mutex.lock();
  playSound(index, channel, start, length);
  mutex.unlock();
}

void APU::music(music_index_t index, int32_t fadeMs, int32_t mask)
{
  mutex.lock();
  startMusic(index, fadeMs, mask);
  mutex.unlock();
}

void APU::playSound(sound_index_t index, channel_index_t channel, int32_t start, int32_t length)
{
  const bool all = channel < 0 || channel >= (channel_index_t)CHANNEL_COUNT;

  if (index == -1 || index == -2)
  {
    for (size_t i = 0; i < CHANNEL_COUNT; ++i)
    {
      if (!all && (channel_index_t)i != channel)
        continue;

      if (index == -1)
      {
        ghostChannel(sfxChannels[i]);
        sfxChannels[i].voice.stop();
        sfxChannels[i].instrument.stop();
      }
      else
      {
        sfxChannels[i].voice.released = true;
        sfxChannels[i].instrument.released = true;
      }
    }
    return;
  }

  if (index < 0 || index >= (sound_index_t)SOUND_COUNT)
    return;

  if (channel == -2)
  {
    for (auto& c : sfxChannels)
      if (c.voice.index == index)
      {
        c.voice.stop();
        c.instrument.stop();
      }
    return;
  }

  if (all)
  {
    /* the same sound restarts where it plays, then free channels outside the music mask are taken, then the oldest sound */
    channel = -1;

    for (size_t i = 0; i < CHANNEL_COUNT && channel < 0; ++i)
      if (sfxChannels[i].voice.index == index)
        channel = i;

    for (size_t i = 0; i < CHANNEL_COUNT && channel < 0; ++i)
      if (!(musicMask & (1 << i)) && !sfxChannels[i].voice.active() && !musicChannels[i].voice.active())
        channel = i;

    for (size_t i = 0; i < CHANNEL_COUNT && channel < 0; ++i)
      if (!(musicMask & (1 << i)) && !sfxChannels[i].voice.active())
        channel = i;

    for (size_t i = 0; i < CHANNEL_COUNT; ++i)
      if (!(musicMask & (1 << i)) && (channel < 0 || (sfxChannels[i].voice.active() && sfxChannels[channel].voice.active() && sfxChannels[i].started < sfxChannels[channel].started)))
        channel = i;

    if (channel < 0)
      return;
  }

  Channel& c = sfxChannels[channel];
  ghostChannel(c);
  c.voice.start(index, std::max(0, start), length > 0 ? length : -1);
  c.instrument.stop();
  c.started = ++starts;
}

void APU::startMusic(music_index_t index, int32_t fadeMs, int32_t mask)
{
  if (index < 0)
  {
    if (fadeMs > 0 && pattern >= 0)
    {
      musicStopping = true;
      musicFade = -1.0f / (fadeMs * (SAMPLE_RATE / 1000.0f));
    }
    else
      stopMusic();
    return;
  }

  musicMask = mask & 0x0f;
  musicStopping = false;
  musicVolume = fadeMs > 0 ? 0.0f : 1.0f;
  musicFade = fadeMs > 0 ? 1.0f / (fadeMs * (SAMPLE_RATE / 1000.0f)) : 0.0f;
  startPattern(index);
}

void APU::stopMusic()
{
  pattern = -1;
  musicStopping = false;
  musicMask = 0;
  for (auto& c : musicChannels)
  {
    c.voice.stop();
    c.instrument.stop();
  }
}

/* the pattern lasts as long as its leftmost sound that does not loop, or 32 notes of its leftmost sound when all of them loop */
void APU::startPattern(music_index_t index)
{
  const Music* m = index >= 0 && index < (music_index_t)MUSIC_COUNT ? memory.music(index) : nullptr;
  bool any = false;

  for (size_t i = 0; m && i < CHANNEL_COUNT; ++i)
    any |= m->isChannelEnabled(i);

  if (!any)
  {
    stopMusic();
    return;
  }

  int64_t length = -1, looping = -1;

  for (size_t i = 0; i < CHANNEL_COUNT; ++i)
  {
    Channel& c = musicChannels[i];
    c.instrument.stop();

    if (!m->isChannelEnabled(i))
    {
      c.voice.stop();
      continue;
    }

    const Sound* sound = memory.sound(m->sound(i));
    ghostChannel(c);
    c.voice.start(m->sound(i), 0, -1);

    const int64_t note = noteSamples(sound, i);
    if (!sound->loops() && length < 0)
      length = note * sound->length();
    if (looping < 0)
      looping = note * 32;
  }

  pattern = index;
  patternPlayed = 0;
  patternLength = length >= 0 ? length : looping;
}

void APU::advancePattern()
{
  const Music* m = memory.music(pattern);

  if (m->isStop())
    stopMusic();
  else if (m->isLoopEnd())
  {
    music_index_t i = pattern;
    while (i > 0 && !memory.music(i)->isLoopBegin())
      --i;
    startPattern(i);
  }
  else
    startPattern(pattern + 1);
}

/* 0x5f40 halves the speed, 0x5f41 adds reverb, 0x5f42 distorts and 0x5f43 damps, one bit per channel */
uint8_t APU::control(int32_t offset) const
{
  return memory.base()[0x5f40 + offset];
}

int32_t APU::noteSamples(const Sound* sound, channel_index_t channel) const
{
  const int32_t speed = std::max<int32_t>(1, sound->speed) * SAMPLES_PER_TICK;
  return (control(0) & (1 << channel)) ? speed * 2 : speed;
}

/* zepto8 fades a channel over 1/130 s when its sound changes sharply, which also matches the PICO-8 recordings best */
int32_t APU::ghostSamples()
{
  return SAMPLE_RATE / 130;
}

void APU::ghostChannel(Channel& c)
{
  if (!c.voice.active() || c.voice.note < 0)
    return;

  c.ghostVoice = c.voice;
  c.ghostVoice.frozen = true;
  c.ghostVoice.fade = 0;
  c.ghostInstrument = c.instrument;
  c.ghostCount = ghostSamples();
}

/* the length that brought instruments that change waveform every note closest to PICO-8 */
int32_t APU::crossfadeSamples()
{
  return 120;
}

float APU::noise(Voice& voice, float frequency, const Sound* sound)
{
  /* zepto8's measurement: random steps through a low pass that follows the pitch, louder the lower the note, worked at 22050 Hz */
  voice.noiseHalf = !voice.noiseHalf;
  if (voice.noiseHalf)
  {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    const float white = (random >> 8) * (2.0f / 16777216.0f) - 1.0f;

    const float scale = frequency / keyHz(63);
    voice.noiseLast = voice.noise;
    voice.noise = (voice.noiseLast + scale * white) / (1.0f + scale);
  }

  const float key = frequency > 0.0f ? 33.0f + 12.0f * std::log2(frequency / 440.0f) : 0.0f;
  const float factor = 1.0f - std::max(0.0f, std::min(63.0f, key)) / 63.0f;
  float out = voice.noise * 1.5f * (1.0f + factor * factor);

  if (sound->flags & 0x02)
  {
    const float t = fraction(voice.phase);
    out *= 2.0f * (t < 0.5f ? t : t - 1.0f);
  }

  return out;
}

float APU::oscillator(Voice& voice, Waveform waveform, float frequency, const Sound* sound)
{
  float out;

  if (waveform == Waveform::NOISE)
    out = noise(voice, frequency, sound);
  else
  {
    const bool buzz = sound->buzz();
    out = wave(waveform, fraction(voice.phase), voice.phase, buzz);

    const int32_t detune = sound->detune();
    if (detune)
    {
      /* organ's second wave two octaves up is a plain triangle */
      const Waveform second = detune == 2 && waveform == Waveform::ORGAN ? Waveform::TRIANGLE : waveform;
      out += 0.5f * wave(second, fraction(voice.detunePhase), voice.detunePhase, buzz);
      voice.detunePhase += frequency * detuneRatio(waveform, detune) / SAMPLE_RATE;
    }
  }

  voice.phase += double(frequency) / SAMPLE_RATE;
  if (voice.phase > 1048576.0)
    voice.phase -= 1048576.0;

  return out;
}

/* instrument is set for a top level sound, whose notes may play sfx 0 to 7 as instruments through that second voice, and
   ghosts is the channel that keeps a copy of the sound to fade out when a note changes */
float APU::renderVoice(Voice& voice, channel_index_t channel, int32_t keyShift, float pitchScale, float volumeScale, Voice* instrument, Channel* ghosts)
{
  if (!voice.active())
    return 0.0f;

  const Sound* sound = memory.sound(voice.index);
  int32_t n = voice.fade > 0 || voice.frozen ? voice.note : int32_t(voice.offset);
  bool looped = false;

  if (n != voice.note)
  {
    if (sound->loops() && !voice.released && voice.note == sound->loopEnd - 1 && n >= sound->loopEnd)
    {
      voice.offset -= sound->loopEnd - sound->loopStart;
      n = int32_t(voice.offset);
      looped = true;
    }

    const bool ended = n >= sound->length() || (voice.limit > 0 && voice.played >= voice.limit);
    if (ended && voice.note < 0)
    {
      voice.stop();
      if (instrument)
        instrument->stop();
      return 0.0f;
    }
  }

  /* the last note carries on while it fades, so the sound does not stop with a click */
  if (voice.fade == 0 && n != voice.note && voice.note >= 0 &&
      (n >= sound->length() || (voice.limit > 0 && voice.played >= voice.limit)))
  {
    voice.fade = END_FADE_SAMPLES;
    n = voice.note;
  }

  if (n != voice.note)
  {
    const bool hadNote = voice.note >= 0;
    if (ghosts && hadNote)
    {
      ghosts->ghostVoice = voice;
      ghosts->ghostVoice.frozen = true;
      ghosts->ghostVoice.fade = 0;
      if (instrument)
        ghosts->ghostInstrument = *instrument;
      else
        ghosts->ghostInstrument.stop();
      ghosts->ghostCount = ghostSamples();
    }

    SoundSample previous = {0};
    if (hadNote)
    {
      previous = sound->samples[voice.note];
      voice.lastKey = previous.pitch() + keyShift;
      voice.lastVolume = previous.volume() / 7.0f;
    }

    voice.note = n;

    const SoundSample& upcoming = sound->samples[n];
    if (hadNote && !previous.useSfx() && !upcoming.useSfx() && previous.waveform() != Waveform::NOISE &&
        previous.volume() > 0)
    {
      voice.crossfade = crossfadeSamples();
      voice.crossfadeWave = uint8_t(previous.waveform());
      voice.crossfadeVolume = voice.volume;
    }

    /* measured on PICO-8: each note restarts its instrument unless it slides on from the same one */
    const SoundSample& next = sound->samples[n];
    if (instrument && next.useSfx())
    {
      const sound_index_t sfx = sound_index_t(next.waveform());
      const bool carries = hadNote && !looped && next.effect() == Effect::SLIDE && previous.useSfx() && previous.waveform() == next.waveform();
      if (!carries || !instrument->active() || instrument->index != sfx)
        instrument->start(sfx, 0, -1);
    }
  }

  const SoundSample note = sound->samples[n];
  const float t = std::min(float(voice.offset - n), 0.999f);
  const int32_t key = note.pitch() + keyShift;
  const float base = keyHz(key);
  float frequency = base;
  float volume = note.volume() / 7.0f;

  switch (note.effect())
  {
  case Effect::SLIDE:
    /* slides from the previous note and its volume, not towards the next one */
    if (voice.lastKey != NO_KEY)
    {
      frequency = lerp(keyHz(voice.lastKey), base, t);
      if (voice.lastVolume > 0.0f)
        volume = lerp(voice.lastVolume, volume, t);
    }
    break;
  case Effect::VIBRATO:
  {
    /* half a semitone at 7.5 Hz whatever the speed */
    const float wobble = std::fabs(fraction(7.5 * voice.clock / SAMPLE_RATE) - 0.5f) - 0.25f;
    frequency = lerp(base, base * 1.059463094f, wobble);
    break;
  }
  case Effect::DROP:
    /* measured on PICO-8: a note that plays an sfx instrument keeps its pitch through a drop */
    if (!(instrument && note.useSfx()))
      frequency *= 1.0f - t;
    break;
  case Effect::FADE_IN:
    volume *= t;
    break;
  case Effect::FADE_OUT:
    volume *= 1.0f - t;
    break;
  case Effect::ARPEGGIO_FAST:
  case Effect::ARPEGGIO_SLOW:
  {
    /* steps through the group of four notes every 4 or 8 ticks, twice as fast at speed 8 or below */
    int64_t ticks = note.effect() == Effect::ARPEGGIO_FAST ? 4 : 8;
    if (sound->speed <= 8)
      ticks /= 2;
    const int32_t step = int32_t(voice.clock / (ticks * SAMPLES_PER_TICK));
    frequency = keyHz(sound->samples[(n & ~3) | (step & 3)].pitch() + keyShift);
    break;
  }
  default:
    break;
  }

  frequency *= pitchScale;
  volume *= volumeScale;

  /* a short glide between volumes keeps note changes from clicking */
  voice.volume += (volume - voice.volume) * 0.04f;

  float out;
  if (instrument && note.useSfx())
    out = renderVoice(*instrument, channel, key - 24, frequency / base, voice.volume, nullptr, nullptr);
  else
  {
    const double phase = voice.phase;
    out = oscillator(voice, note.waveform(), frequency, sound) * voice.volume;

    if (voice.crossfade > 0)
    {
      const float k = float(voice.crossfade) / float(crossfadeSamples());
      const float before = wave(Waveform(voice.crossfadeWave), fraction(phase), phase, sound->buzz()) * voice.crossfadeVolume;
      out = out * (1.0f - k) + before * k;
      --voice.crossfade;
    }
  }

  if (voice.frozen)
  {
    ++voice.clock;
    return out;
  }

  if (voice.fade > 0)
  {
    out *= float(voice.fade) / float(END_FADE_SAMPLES);
    if (--voice.fade == 0)
    {
      voice.stop();
      if (instrument)
        instrument->stop();
    }
    ++voice.clock;
    return out;
  }

  const double step = 1.0 / noteSamples(sound, channel);
  voice.offset += step;
  voice.played += step;
  ++voice.clock;

  return out;
}

float APU::renderChannel(Channel& c, channel_index_t index)
{
  if (c.voice.active())
    c.flags = memory.sound(c.voice.index)->flags;

  float out = renderVoice(c.voice, index, 0, 1.0f, 1.0f, &c.instrument, &c);

  if (c.ghostCount > 0)
  {
    const float k = float(c.ghostCount) / float(ghostSamples());
    const float before = renderVoice(c.ghostVoice, index, 0, 1.0f, 1.0f, &c.ghostInstrument, nullptr);
    out = out * (1.0f - k) + before * k;
    if (--c.ghostCount == 0)
    {
      c.ghostVoice.stop();
      c.ghostInstrument.stop();
    }
  }

  Sound flags;
  flags.flags = c.flags;
  const uint8_t bit = 1 << index, strong = 0x10 << index;

  /* reverb echoes the channel two or four ticks later, as zepto8 measured */
  int32_t reverb = flags.reverb();
  if (control(1) & bit) reverb = std::max(reverb, 1);
  if (control(1) & strong) reverb = 2;
  if (reverb)
  {
    const size_t delay = reverb == 1 ? 2 * SAMPLES_PER_TICK : 4 * SAMPLES_PER_TICK;
    const float echoed = c.echo[(c.echoAt + c.echo.size() - delay) % c.echo.size()];
    out += echoed * 0.5f;
    c.echo[c.echoAt] = out;
  }
  else
    c.echo[c.echoAt] = 0.0f;
  c.echoAt = (c.echoAt + 1) % c.echo.size();

  /* both shelves run all the time so switching dampen on mid sound starts from a settled state */
  int32_t dampen = flags.dampen();
  if (control(3) & bit) dampen = std::max(dampen, 1);
  if (control(3) & strong) dampen = 2;
  const float light = dampLight.run(c.dampLight, out);
  const float strongOut = dampStrong.run(c.dampStrong, out);
  if (dampen == 1)
    out = light;
  else if (dampen == 2)
    out = strongOut;

  if (control(2) & bit)
    out = std::max(-0.25f, std::min(0.25f, out * 3.0f));

  return out;
}

const Voice* APU::audible(channel_index_t channel) const
{
  if (sfxChannels[channel].voice.active())
    return &sfxChannels[channel].voice;
  if (musicChannels[channel].voice.active())
    return &musicChannels[channel].voice;
  return nullptr;
}

int APU::channelSound(int channel) const
{
  if (channel < 0 || channel >= (int)CHANNEL_COUNT)
    return -1;
  const Voice* voice = audible(channel);
  return voice ? voice->index : -1;
}

int APU::channelNote(int channel) const
{
  if (channel < 0 || channel >= (int)CHANNEL_COUNT)
    return -1;
  const Voice* voice = audible(channel);
  return voice ? (voice->note >= 0 ? voice->note : int(voice->offset)) : -1;
}

void APU::renderSounds(int16_t* dest, size_t totalSamples)
{
  mutex.lock();

  for (size_t s = 0; s < totalSamples; ++s)
  {
    if (pattern >= 0 && ++patternPlayed > patternLength)
      advancePattern();

    if (pattern >= 0 && musicFade != 0.0f)
    {
      musicVolume = std::max(0.0f, std::min(1.0f, musicVolume + musicFade));
      if (musicStopping && musicVolume <= 0.0f)
        stopMusic();
      else if (!musicStopping && musicVolume >= 1.0f)
        musicFade = 0.0f;
    }

    float mix = 0.0f;

    for (size_t i = 0; i < CHANNEL_COUNT; ++i)
    {
      const bool overridden = sfxChannels[i].voice.active();
      const float music = renderChannel(musicChannels[i], i) * musicVolume;
      const float sound = renderChannel(sfxChannels[i], i);

      if (_soundEnabled)
        mix += sound;
      if (_musicEnabled && !overridden)
        mix += music;
    }

    mix = lowpass(mix);

    /* four channels at once can pass full scale, and a hard clip there is what makes busy moments sound crunchy,
       so anything above the knee bends towards full scale instead */
    constexpr float knee = 0.6f;
    const float level = std::fabs(mix);
    if (level > knee)
      mix = std::copysign(knee + (1.0f - knee) * std::tanh((level - knee) / (1.0f - knee)), mix);

    dest[s] = int16_t(std::max(-32767.0f, std::min(32767.0f, mix * 32767.0f)));
  }

  mutex.unlock();
}

namespace
{
  template<typename T> void putRaw(std::string& out, const T& value)
  {
    static_assert(std::is_trivially_copyable<T>::value, "save state fields must be plain data");
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
  }

  template<typename T> bool getRaw(const char*& at, const char* end, T& value)
  {
    if (size_t(end - at) < sizeof(T)) return false;
    std::memcpy(&value, at, sizeof(T));
    at += sizeof(T);
    return true;
  }
}

namespace
{
  /* "SND2" leads every sound state since the dampen filter became two shelves, so a later layout change is caught instead of read out of line */
  constexpr uint32_t SOUND_STATE_TAG = 0x32444e53;

  /* the channel as the 18 October 2026 build saved it, before dampen became two shelves */
  struct LegacyChannel
  {
    Voice voice;
    Voice instrument;
    Voice ghostVoice;
    Voice ghostInstrument;
    int32_t ghostCount;
    uint32_t started;
    uint8_t flags;
    float damped;
    std::array<float, 4096> echo;
    size_t echoAt;
  };

  Channel fromLegacy(const LegacyChannel& old)
  {
    Channel c;
    c.voice = old.voice;
    c.instrument = old.instrument;
    c.ghostVoice = old.ghostVoice;
    c.ghostInstrument = old.ghostInstrument;
    c.ghostCount = old.ghostCount;
    c.started = old.started;
    c.flags = old.flags;
    c.echo = old.echo;
    c.echoAt = old.echoAt;
    return c;
  }

  template<typename C> struct SoundState
  {
    std::array<C, 4> sfxChannels;
    std::array<C, 4> musicChannels;
    int32_t pattern;
    int64_t patternPlayed;
    int64_t patternLength;
    uint8_t musicMask;
    float musicVolume;
    float musicFade;
    bool musicStopping;
    uint32_t starts;
    uint32_t random;
    std::array<float, 127> lowpassHistory;
    size_t lowpassAt;
  };

  template<typename C> bool readState(const char* at, const char* end, SoundState<C>& state)
  {
    return getRaw(at, end, state.sfxChannels) && getRaw(at, end, state.musicChannels) && getRaw(at, end, state.pattern) &&
      getRaw(at, end, state.patternPlayed) && getRaw(at, end, state.patternLength) && getRaw(at, end, state.musicMask) &&
      getRaw(at, end, state.musicVolume) && getRaw(at, end, state.musicFade) && getRaw(at, end, state.musicStopping) &&
      getRaw(at, end, state.starts) && getRaw(at, end, state.random) && getRaw(at, end, state.lowpassHistory) &&
      getRaw(at, end, state.lowpassAt) && at == end;
  }

  template<typename C> size_t stateSize()
  {
    return sizeof(std::array<C, 4>) * 2 + sizeof(int32_t) + sizeof(int64_t) * 2 + sizeof(uint8_t) + sizeof(float) * 2 +
      sizeof(bool) + sizeof(uint32_t) * 2 + sizeof(std::array<float, 127>) + sizeof(size_t);
  }

  template<typename C> bool plausible(const SoundState<C>& state)
  {
    if (state.lowpassAt >= 127 || state.pattern < -1 || state.pattern >= int32_t(MUSIC_COUNT)) return false;
    if (!std::isfinite(state.musicVolume) || !std::isfinite(state.musicFade)) return false;

    for (const float sample : state.lowpassHistory)
      if (!std::isfinite(sample)) return false;

    return true;
  }
}

void APU::saveState(std::string& out)
{
  mutex.lock();
  out.clear();
  putRaw(out, SOUND_STATE_TAG);
  putRaw(out, sfxChannels);
  putRaw(out, musicChannels);
  putRaw(out, pattern);
  putRaw(out, patternPlayed);
  putRaw(out, patternLength);
  putRaw(out, musicMask);
  putRaw(out, musicVolume);
  putRaw(out, musicFade);
  putRaw(out, musicStopping);
  putRaw(out, starts);
  putRaw(out, random);
  putRaw(out, lowpassHistory);
  putRaw(out, lowpassAt);
  mutex.unlock();
}

namespace
{
  template<typename C> void copyState(const SoundState<C>& from, SoundState<Channel>& to, Channel (*convert)(const C&))
  {
    for (size_t i = 0; i < from.sfxChannels.size(); ++i)
    {
      to.sfxChannels[i] = convert(from.sfxChannels[i]);
      to.musicChannels[i] = convert(from.musicChannels[i]);
    }

    to.pattern = from.pattern;
    to.patternPlayed = from.patternPlayed;
    to.patternLength = from.patternLength;
    to.musicMask = from.musicMask;
    to.musicVolume = from.musicVolume;
    to.musicFade = from.musicFade;
    to.musicStopping = from.musicStopping;
    to.starts = from.starts;
    to.random = from.random;
    to.lowpassHistory = from.lowpassHistory;
    to.lowpassAt = from.lowpassAt;
  }
}

bool APU::loadState(const std::string& in)
{
  const char* at = in.data();
  const char* end = at + in.size();

  uint32_t tag = 0;
  const bool tagged = getRaw(at, end, tag) && tag == SOUND_STATE_TAG;

  std::unique_ptr<SoundState<Channel>> current(new SoundState<Channel>());
  std::unique_ptr<SoundState<LegacyChannel>> legacy;

  bool ok = false;
  if (tagged)
  {
    ok = readState(at, end, *current) && plausible(*current);
  }
  else if (in.size() == stateSize<LegacyChannel>())
  {
    legacy.reset(new SoundState<LegacyChannel>());
    ok = readState(in.data(), end, *legacy) && plausible(*legacy);
  }

  if (ok && legacy) copyState(*legacy, *current, fromLegacy);

  mutex.lock();
  if (ok)
  {
    sfxChannels = current->sfxChannels;
    musicChannels = current->musicChannels;
    pattern = current->pattern;
    patternPlayed = current->patternPlayed;
    patternLength = current->patternLength;
    musicMask = current->musicMask;
    musicVolume = current->musicVolume;
    musicFade = current->musicFade;
    musicStopping = current->musicStopping;
    starts = current->starts;
    random = current->random ? current->random : 0x12345678;
    lowpassHistory = current->lowpassHistory;
    lowpassAt = current->lowpassAt;
  }
  else
  {
    init();
    random = 0x12345678;
  }
  mutex.unlock();

  return ok;
}

#endif
