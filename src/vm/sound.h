#pragma once

#include "defines.h"
#include "common.h"

#include <array>
#include <string>
#include <vector>
#ifdef __LIBRETRO__
#include "slock_wrapper.h"
#endif
#ifndef USE_SLOCK_WRAPPER
#include <mutex>
#endif

#if SOUND_ENABLED

namespace retro8
{
  class Memory;

  namespace sfx
  {
    using volume_t = int32_t;
    using pitch_t = int32_t;
    using channel_index_t = int32_t;
    using sound_index_t = int32_t;
    using music_index_t = int32_t;

    enum class Waveform
    {
      TRIANGLE, TILTED_SAW, SAW, SQUARE, PULSE, ORGAN, NOISE, PHASER
    };

    enum class Effect
    {
      NONE, SLIDE, VIBRATO, DROP, FADE_IN, FADE_OUT, ARPEGGIO_FAST, ARPEGGIO_SLOW
    };

    /* one note as PICO-8 stores it: pitch in bits 0-5, waveform 6-8, volume 9-11, effect 12-14, and bit 15 plays sfx 0-7 as the instrument */
    struct SoundSample
    {
      static constexpr uint16_t CustomMask = 0x8000;
      static constexpr uint16_t EffectMask = 0x7000;
      static constexpr uint16_t VolumeMask = 0x0E00;
      static constexpr uint16_t WaveformMask = 0x01C0;
      static constexpr uint16_t PitchMask = 0x003F;

      static constexpr uint32_t EffectShift = 12;
      static constexpr uint32_t VolumeShift = 9;
      static constexpr uint32_t WaveformShift = 6;

      uint16_t value;

      inline bool useSfx() const { return value & CustomMask; }
      inline Effect effect() const { return Effect((value & EffectMask) >> EffectShift); }
      inline Waveform waveform() const { return Waveform((value & WaveformMask) >> WaveformShift); }
      inline volume_t volume() const { return (value & VolumeMask) >> VolumeShift; }
      inline pitch_t pitch() const { return value & PitchMask; }

      void setPitch(pitch_t pitch) { value = (value & ~PitchMask) | pitch; }
      void setVolume(volume_t volume) { value = (value & ~VolumeMask) | (volume << VolumeShift); }
      void setEffect(Effect effect) { value = (value & ~EffectMask) | (uint16_t(effect) << EffectShift); }
      void setWaveform(Waveform waveform) { value = (value & ~WaveformMask) | (uint16_t(waveform) << WaveformShift); }
      void setCustom(bool custom) { value = (value & ~CustomMask) | (custom ? CustomMask : 0); }
    };

    struct Sound
    {
      std::array<SoundSample, 32> samples;
      uint8_t flags; // editor mode, noiz and buzz bits, then detune, reverb and dampen packed in base 3 from 8 up
      uint8_t speed; // one note lasts this many ticks of 183 samples at 22050 Hz
      uint8_t loopStart;
      uint8_t loopEnd;

      bool loops() const { return loopEnd > loopStart; }
      /* a loop start without a loop end makes the sound that many notes long */
      int32_t length() const { return !loops() && loopEnd == 0 && loopStart > 0 ? loopStart : 32; }

      int32_t detune() const { return (flags / 8) % 3; }
      int32_t reverb() const { return (flags / 24) % 3; }
      int32_t dampen() const { return (flags / 72) % 3; }
      bool buzz() const { return flags & 0x04; }
    };

    /* a music pattern: bit 6 of each byte switches its channel off, and bit 7 of the first three marks loop start, loop back and stop */
    struct Music
    {
    private:
      constexpr static uint8_t SOUND_INDEX_MASK = 0b00111111;
      constexpr static uint8_t DISABLED_FLAG = 0b01000000;
      constexpr static uint8_t FLAG = 0b10000000;

      std::array<uint8_t, 4> indices;

    public:
      void setSound(channel_index_t channel, uint8_t value) { indices[channel] = (indices[channel] & FLAG) | (value & ~FLAG); }
      void markLoopBegin() { indices[0] |= FLAG; }
      void markLoopEnd() { indices[1] |= FLAG; }
      void markStop() { indices[2] |= FLAG; }

      inline bool isLoopBegin() const { return (indices[0] & FLAG) != 0; }
      inline bool isLoopEnd() const { return (indices[1] & FLAG) != 0; }
      inline bool isStop() const { return (indices[2] & FLAG) != 0; }

      inline bool isChannelEnabled(channel_index_t channel) const { return (indices[channel] & DISABLED_FLAG) == 0; }
      sound_index_t sound(channel_index_t channel) const { return indices[channel] & SOUND_INDEX_MASK; }
    };

    using sound_t = Sound;
    using music_t = Music;

    static constexpr size_t SOUND_COUNT = 64;
    static constexpr size_t MUSIC_COUNT = 64;
    static constexpr int32_t NO_KEY = -1000;

    /* a sound being played by sfx() or music(), or as the instrument of a note */
    struct Voice
    {
      sound_index_t index = -1;
      int32_t note = -1;
      int32_t limit = -1;
      double offset = 0.0;
      double played = 0.0;
      int64_t clock = 0;
      bool released = false;

      int32_t lastKey = NO_KEY;
      float lastVolume = 0.0f;
      float volume = 0.0f;

      double phase = 0.0;
      double detunePhase = 0.0;
      float noise = 0.0f;
      float noiseLast = 0.0f;
      bool noiseHalf = false;

      /* samples left of the fade a sound ends with, 0 while it is playing normally */
      int32_t fade = 0;

      /* the note just left, faded out over the first samples of the next so a change of waveform does not click */
      int32_t crossfade = 0;
      uint8_t crossfadeWave = 0;
      float crossfadeVolume = 0.0f;

      /* a copy kept on its note while it fades out under whatever replaced it */
      bool frozen = false;

      bool active() const { return index >= 0; }
      void start(sound_index_t sound, int32_t first, int32_t notes);
      void stop() { index = -1; note = -1; fade = 0; }
    };

    /* the last two inputs and outputs of a biquad */
    struct Shelf
    {
      float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    struct Channel
    {
      Voice voice;
      Voice instrument;

      /* what the channel played before its last sharp change, faded out over the next samples */
      Voice ghostVoice;
      Voice ghostInstrument;
      int32_t ghostCount = 0;
      uint32_t started = 0;
      uint8_t flags = 0;

      Shelf dampLight;
      Shelf dampStrong;
      std::array<float, 4096> echo = {};
      size_t echoAt = 0;
    };

    class APU
    {
    public:
      static constexpr size_t CHANNEL_COUNT = 4;
      static constexpr int32_t SAMPLE_RATE = 44100;
      static constexpr int32_t SAMPLES_PER_TICK = 366;
      static constexpr size_t LOWPASS_TAPS = 127;
      /* PICO-8 ends a sound with a short straight fade rather than cutting it, measured at 16 samples here */
      static constexpr int32_t END_FADE_SAMPLES = 16;

    private:
      retro8::Memory& memory;

      std::array<Channel, CHANNEL_COUNT> sfxChannels;
      std::array<Channel, CHANNEL_COUNT> musicChannels;

      music_index_t pattern = -1;
      int64_t patternPlayed = 0;
      int64_t patternLength = 0;
      uint8_t musicMask = 0;
      float musicVolume = 1.0f;
      float musicFade = 0.0f;
      bool musicStopping = false;
      uint32_t starts = 0;
      uint32_t random = 0x12345678;

      std::array<float, LOWPASS_TAPS> lowpassTaps = {};
      std::array<float, LOWPASS_TAPS> lowpassHistory = {};
      size_t lowpassAt = 0;

      void initLowpass();
      float lowpass(float sample);

#ifdef USE_SLOCK_WRAPPER
      slock_wrap mutex;
#else
      std::mutex mutex;
#endif

      bool _soundEnabled, _musicEnabled;

      void playSound(sound_index_t index, channel_index_t channel, int32_t start, int32_t length);
      void startMusic(music_index_t index, int32_t fadeMs, int32_t mask);
      void startPattern(music_index_t index);
      void stopMusic();
      void advancePattern();

      uint8_t control(int32_t offset) const;
      int32_t noteSamples(const Sound* sound, channel_index_t channel) const;
      float renderVoice(Voice& voice, channel_index_t channel, int32_t keyShift, float pitchScale, float volumeScale, Voice* instrument, Channel* ghosts);
      static int32_t ghostSamples();
      static void ghostChannel(Channel& c);
      float renderChannel(Channel& channel, channel_index_t index);
      float oscillator(Voice& voice, Waveform waveform, float frequency, const Sound* sound);
      float noise(Voice& voice, float frequency, const Sound* sound);
      static int32_t crossfadeSamples();

      const Voice* audible(channel_index_t channel) const;

    public:
      APU(Memory& memory) : memory(memory), _soundEnabled(true), _musicEnabled(true) { initLowpass(); }

      void init();

      /* index -1 stops and -2 releases loops; a length below 1 plays to the end */
      void play(sound_index_t index, channel_index_t channel, int32_t start, int32_t length);
      void music(music_index_t index, int32_t fadeMs, int32_t mask);

      void renderSounds(int16_t* dest, size_t samples);

      /* the channels, music position and filters, for save states */
      void saveState(std::string& out);
      bool loadState(const std::string& in);

      int channelSound(int channel) const;
      int channelNote(int channel) const;
      int musicPattern() const { return pattern; }
      int musicTicks() const { return pattern >= 0 ? int(patternPlayed / SAMPLES_PER_TICK) : 0; }

      bool isMusicEnabled() const { return _musicEnabled; }
      bool isSoundEnabled() const { return _soundEnabled; }

      void toggleSound(bool active) { _soundEnabled = active; }
      void toggleMusic(bool active) { _musicEnabled = active; }
    };
  }
}

#endif
