#include "app/hardware_audio.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <QByteArray>
#include <QFile>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <optional>
#include <vector>

namespace p2000c {
namespace {

constexpr int kGeneratedSampleRate = 44'100;
constexpr double kSampleNormalizationPeak = 0.707945784;  // -3 dBFS.

struct Sound {
  QByteArray pcm;
  int sample_rate = kGeneratedSampleRate;
};

std::uint16_t little_u16(const char* data) {
  return static_cast<std::uint8_t>(data[0]) |
         (static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[1])) << 8);
}

std::uint32_t little_u32(const char* data) {
  return little_u16(data) |
         (static_cast<std::uint32_t>(little_u16(data + 2)) << 16);
}

/** Loads a conventional mono 16-bit PCM WAV from the embedded resources. */
std::optional<Sound> load_wav(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const QByteArray bytes = file.readAll();
  if (bytes.size() < 12 || std::memcmp(bytes.constData(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.constData() + 8, "WAVE", 4) != 0) {
    return std::nullopt;
  }
  int sample_rate = 0;
  QByteArray pcm;
  for (qsizetype offset = 12; offset + 8 <= bytes.size();) {
    const char* chunk = bytes.constData() + offset;
    const std::uint32_t length = little_u32(chunk + 4);
    const qsizetype data_offset = offset + 8;
    if (length > static_cast<std::uint32_t>(bytes.size() - data_offset)) {
      break;
    }
    if (std::memcmp(chunk, "fmt ", 4) == 0 && length >= 16 &&
        little_u16(bytes.constData() + data_offset) == 1 &&
        little_u16(bytes.constData() + data_offset + 2) == 1 &&
        little_u16(bytes.constData() + data_offset + 14) == 16) {
      sample_rate = static_cast<int>(
          little_u32(bytes.constData() + data_offset + 4));
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      pcm = bytes.mid(data_offset, static_cast<qsizetype>(length));
    }
    offset = data_offset + static_cast<qsizetype>((length + 1U) & ~1U);
  }
  if (sample_rate <= 0 || pcm.isEmpty()) {
    return std::nullopt;
  }

  // The MAME recordings intentionally retain considerable capture headroom;
  // their peaks vary from roughly -19 to -36 dBFS. Normalize each mechanical
  // event before applying its role-specific mixer gain so the motor remains
  // quieter than seeks without making the entire drive barely audible.
  std::int32_t peak = 0;
  for (qsizetype offset = 0;
       offset + static_cast<qsizetype>(sizeof(std::int16_t)) <= pcm.size();
       offset += sizeof(std::int16_t)) {
    std::int16_t sample = 0;
    std::memcpy(&sample, pcm.constData() + offset, sizeof(sample));
    peak = std::max(peak, std::abs(static_cast<std::int32_t>(sample)));
  }
  if (peak > 0) {
    const double scale =
        kSampleNormalizationPeak * 32767.0 / static_cast<double>(peak);
    for (qsizetype offset = 0;
         offset + static_cast<qsizetype>(sizeof(std::int16_t)) <= pcm.size();
         offset += sizeof(std::int16_t)) {
      std::int16_t sample = 0;
      std::memcpy(&sample, pcm.constData() + offset, sizeof(sample));
      const auto normalized = static_cast<std::int16_t>(std::clamp(
          std::lround(static_cast<double>(sample) * scale), -32768L, 32767L));
      std::memcpy(pcm.data() + offset, &normalized, sizeof(normalized));
    }
  }
  return Sound{std::move(pcm), sample_rate};
}

std::int16_t sample_value(double value) {
  return static_cast<std::int16_t>(
      std::clamp(value, -1.0, 1.0) * 32767.0);
}

Sound samples_to_sound(const std::vector<std::int16_t>& samples,
                       int sample_rate = kGeneratedSampleRate) {
  QByteArray pcm(static_cast<qsizetype>(samples.size() * sizeof(samples[0])),
                 Qt::Uninitialized);
  std::memcpy(pcm.data(), samples.data(), static_cast<std::size_t>(pcm.size()));
  return {std::move(pcm), sample_rate};
}

Sound terminal_bell() {
  constexpr double duration = 0.135;
  std::vector<std::int16_t> samples(
      static_cast<std::size_t>(duration * kGeneratedSampleRate));
  double filtered = 0.0;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const double time = static_cast<double>(index) / kGeneratedSampleRate;
    const double square = std::sin(2.0 * std::numbers::pi * 1300.0 * time) >= 0
                              ? 1.0
                              : -1.0;
    filtered += (square - filtered) * 0.24;
    const double attack = std::min(1.0, time / 0.004);
    const double envelope = attack * std::exp(-time * 9.0);
    samples[index] = sample_value(filtered * envelope * 0.20);
  }
  return samples_to_sound(samples);
}

/** Crops or repeats a seek train to the actual number of 6 ms head steps. */
Sound seek_for_distance(const Sound& source, int distance) {
  const std::size_t frame_count = static_cast<std::size_t>(
      std::clamp(15 + std::max(1, distance) * 6, 25, 500) *
      source.sample_rate / 1000);
  const std::size_t source_frames =
      static_cast<std::size_t>(source.pcm.size()) / sizeof(std::int16_t);
  std::vector<std::int16_t> result(frame_count);
  const auto* input =
      reinterpret_cast<const std::int16_t*>(source.pcm.constData());
  for (std::size_t index = 0; index < frame_count; ++index) {
    result[index] = input[index % source_frames];
  }
  const std::size_t fade_frames =
      std::min(frame_count, static_cast<std::size_t>(source.sample_rate / 100));
  for (std::size_t index = 0; index < fade_frames; ++index) {
    const double gain = static_cast<double>(fade_frames - index) / fade_frames;
    result[frame_count - fade_frames + index] = static_cast<std::int16_t>(
        result[frame_count - fade_frames + index] * gain);
  }
  return samples_to_sound(result, source.sample_rate);
}

}  // namespace

struct HardwareAudio::Impl {
  struct Voice {
    ALuint source = 0;
    ALuint buffer = 0;
    float base_gain = 1.0F;
  };

  ALCdevice* device = nullptr;
  ALCcontext* context = nullptr;
  std::vector<Voice> voices;
  Voice floppy_motor;
  QTimer floppy_idle_timer;
  std::size_t next_voice = 0;
  float master_volume = 0.7F;
  bool initialization_attempted = false;
  bool floppy_motor_active = false;

  Impl() {
    floppy_idle_timer.setSingleShot(true);
    QObject::connect(&floppy_idle_timer, &QTimer::timeout,
                     [this]() { stop_floppy_motor(); });
  }

  bool initialize() {
    if (initialization_attempted) {
      return context != nullptr;
    }
    initialization_attempted = true;
    device = alcOpenDevice(nullptr);
    if (device == nullptr) {
      return false;
    }
    context = alcCreateContext(device, nullptr);
    if (context == nullptr || alcMakeContextCurrent(context) == ALC_FALSE) {
      if (context != nullptr) {
        alcDestroyContext(context);
        context = nullptr;
      }
      alcCloseDevice(device);
      device = nullptr;
      return false;
    }
    voices.resize(6);
    for (Voice& voice : voices) {
      alGenSources(1, &voice.source);
    }
    alGenSources(1, &floppy_motor.source);
    return true;
  }

  void clear_voice(Voice* voice) {
    alSourceStop(voice->source);
    alSourcei(voice->source, AL_BUFFER, 0);
    if (voice->buffer != 0) {
      alDeleteBuffers(1, &voice->buffer);
      voice->buffer = 0;
    }
  }

  void play(const Sound& sound, float gain = 1.0F, bool loop = false,
            Voice* dedicated_voice = nullptr) {
    if (!initialize() || sound.pcm.isEmpty()) {
      return;
    }
    Voice* voice = dedicated_voice != nullptr
                       ? dedicated_voice
                       : &voices[next_voice++ % voices.size()];
    clear_voice(voice);
    voice->base_gain = gain;
    alGenBuffers(1, &voice->buffer);
    alBufferData(voice->buffer, AL_FORMAT_MONO16, sound.pcm.constData(),
                 static_cast<ALsizei>(sound.pcm.size()), sound.sample_rate);
    alSourcef(voice->source, AL_GAIN, gain * master_volume);
    alSourcei(voice->source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcei(voice->source, AL_BUFFER, static_cast<ALint>(voice->buffer));
    alSourcePlay(voice->source);
  }

  void set_volume(double volume) {
    master_volume = static_cast<float>(std::clamp(volume, 0.0, 1.0));
    if (context == nullptr) {
      return;
    }
    for (Voice& voice : voices) {
      alSourcef(voice.source, AL_GAIN, voice.base_gain * master_volume);
    }
    alSourcef(floppy_motor.source, AL_GAIN,
              floppy_motor.base_gain * master_volume);
  }

  void start_floppy_motor() {
    if (floppy_motor_active) {
      return;
    }
    floppy_motor_active = true;
    static const auto start =
        load_wav(":/audio/525_spin_start_loaded.wav");
    static const auto running = load_wav(":/audio/525_spin_loaded.wav");
    if (start.has_value()) {
      play(*start, 0.52F);
    }
    if (running.has_value()) {
      play(*running, 0.24F, true, &floppy_motor);
    }
  }

  void stop_floppy_motor() {
    floppy_idle_timer.stop();
    if (!floppy_motor_active) {
      return;
    }
    floppy_motor_active = false;
    if (context != nullptr) {
      clear_voice(&floppy_motor);
    }
    static const auto stop = load_wav(":/audio/525_spin_end.wav");
    if (stop.has_value()) {
      play(*stop, 0.45F);
    }
  }

  void touch_floppy_motor(int operation_duration_ms) {
    start_floppy_motor();
    floppy_idle_timer.start(std::max(HardwareAudio::kFloppyIdleTimeoutMs,
                                    operation_duration_ms + 500));
  }

  void stop_all() {
    floppy_idle_timer.stop();
    floppy_motor_active = false;
    if (context == nullptr) {
      return;
    }
    clear_voice(&floppy_motor);
    for (Voice& voice : voices) {
      clear_voice(&voice);
    }
  }

  void play_floppy_seek(int distance) {
    static const auto step = load_wav(":/audio/525_step_1_1.wav");
    static const auto seek = load_wav(":/audio/525_seek_6ms.wav");
    const auto& sample = distance <= 1 ? step : seek;
    if (!sample.has_value()) {
      return;
    }
    play(distance <= 1 ? *sample : seek_for_distance(*sample, distance),
         distance <= 1 ? 0.42F : 0.48F);
  }

  ~Impl() {
    floppy_idle_timer.stop();
    if (context == nullptr) {
      return;
    }
    alcMakeContextCurrent(context);
    clear_voice(&floppy_motor);
    alDeleteSources(1, &floppy_motor.source);
    for (Voice& voice : voices) {
      clear_voice(&voice);
      alDeleteSources(1, &voice.source);
    }
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context);
    alcCloseDevice(device);
  }
};

HardwareAudio::HardwareAudio() : impl_(std::make_unique<Impl>()) {}
HardwareAudio::~HardwareAudio() = default;

void HardwareAudio::set_volume(double volume) { impl_->set_volume(volume); }

double HardwareAudio::volume() const { return impl_->master_volume; }

void HardwareAudio::stop_all() { impl_->stop_all(); }

bool HardwareAudio::floppy_motor_active() const {
  return impl_->floppy_motor_active;
}

void HardwareAudio::play_bell() { impl_->play(terminal_bell()); }

void HardwareAudio::play_storage_activity(
    const P2000cMachine::StorageActivity& activity) {
  using Device = P2000cMachine::StorageDevice;
  using Operation = P2000cMachine::StorageOperation;
  if (activity.device == Device::kFloppy) {
    if (activity.operation == Operation::kMotorStop) {
      impl_->stop_floppy_motor();
    } else {
      impl_->touch_floppy_motor(activity.duration_ms);
      if (activity.operation == Operation::kSeek) {
        impl_->play_floppy_seek(activity.distance);
      }
    }
    // Reading and writing magnetic flux is effectively silent. The rotating
    // disk and the head motion already provide the authentic audible cues.
    return;
  }
  // MAME provides no genuine hard-disk mechanics samples, so SASI activity is
  // intentionally silent rather than represented by an invented effect.
}

}  // namespace p2000c
