#define NOMINMAX
#include "AudioEngine.h"

#include "../Core/Config.h"
#include "../Core/ModLogger.h"
#include "../../sdk/inc/natives.h"

#include <xaudio2.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace AudioEngine {
namespace {

struct Sample {
  std::string name;
  std::vector<std::uint8_t> pcm;
};

struct Bank {
  std::vector<Sample> samples;
  int lastIndex = -1;
  ULONGLONG lastPlayedAt = 0;
};

struct WaveHeader {
  std::uint16_t format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bits = 0;
};

IXAudio2 *s_audio = nullptr;
IXAudio2MasteringVoice *s_master = nullptr;
std::array<IXAudio2SourceVoice *, 16> s_voices{};
std::mt19937 s_rng{std::random_device{}()};
bool s_ready = false;

Bank s_carShift;
Bank s_carSoft;
Bank s_carPower;
Bank s_bikeUp;
Bank s_bikeDown;
Bank s_bikeUpSoft;
Bank s_bikeDownSoft;
Bank s_bikeError;
Bank s_parkApply;
Bank s_parkRelease;
Bank s_autoSelector;

std::uint32_t ReadU32(std::ifstream &file) {
  std::uint32_t value = 0;
  file.read(reinterpret_cast<char *>(&value), sizeof(value));
  return value;
}

std::uint16_t ReadU16(std::ifstream &file) {
  std::uint16_t value = 0;
  file.read(reinterpret_cast<char *>(&value), sizeof(value));
  return value;
}

bool LoadWave(const std::filesystem::path &path, Sample &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return false;

  char riff[4]{};
  file.read(riff, 4);
  (void)ReadU32(file);
  char wave[4]{};
  file.read(wave, 4);
  if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE")
    return false;

  WaveHeader header{};
  std::vector<std::uint8_t> pcm;
  while (file && (!header.format || pcm.empty())) {
    char id[4]{};
    file.read(id, 4);
    if (!file)
      break;
    const std::uint32_t size = ReadU32(file);
    const std::streampos chunkStart = file.tellg();
    if (std::string(id, 4) == "fmt ") {
      header.format = ReadU16(file);
      header.channels = ReadU16(file);
      header.sampleRate = ReadU32(file);
      (void)ReadU32(file);
      (void)ReadU16(file);
      header.bits = ReadU16(file);
    } else if (std::string(id, 4) == "data") {
      pcm.resize(size);
      file.read(reinterpret_cast<char *>(pcm.data()), size);
    }
    file.seekg(chunkStart + static_cast<std::streamoff>(size + (size & 1u)));
  }

  if (header.format != WAVE_FORMAT_PCM || header.channels != 2 ||
      header.sampleRate != 44100 || header.bits != 16 || pcm.empty()) {
    LOG_WARN(Audio,
             "Skip WAV unsupported: %ls fmt=%u ch=%u hz=%u bits=%u",
             path.c_str(), header.format, header.channels, header.sampleRate,
             header.bits);
    return false;
  }

  auto *samples = reinterpret_cast<std::int16_t *>(pcm.data());
  const std::size_t count = pcm.size() / sizeof(std::int16_t);
  int peak = 1;
  for (std::size_t i = 0; i < count; ++i)
    peak = std::max(peak, std::abs(static_cast<int>(samples[i])));

  // Peak limiter per voice. Master masih menyisakan headroom buat overlap.
  const float ceiling = std::clamp(Config::AudioLimiterCeiling, 0.25f, 0.95f);
  const float gain = std::min(1.0f, ceiling * 32767.0f / peak);
  if (gain < 0.999f) {
    for (std::size_t i = 0; i < count; ++i) {
      samples[i] = static_cast<std::int16_t>(
          std::clamp(static_cast<int>(samples[i] * gain), -32767, 32767));
    }
  }

  out.name = path.filename().string();
  out.pcm = std::move(pcm);
  return true;
}

void LoadBank(const std::filesystem::path &root, Bank &bank,
              std::initializer_list<const wchar_t *> names) {
  bank.samples.clear();
  bank.lastIndex = -1;
  bank.lastPlayedAt = 0;
  for (const wchar_t *name : names) {
    Sample sample;
    const auto path = root / name;
    if (LoadWave(path, sample)) {
      LOG_INFO(Audio, "Loaded %s (%zu bytes)", sample.name.c_str(),
               sample.pcm.size());
      bank.samples.push_back(std::move(sample));
    }
  }
}

IXAudio2SourceVoice *AcquireVoice() {
  for (auto *voice : s_voices) {
    XAUDIO2_VOICE_STATE state{};
    voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued == 0)
      return voice;
  }
  return nullptr;
}

bool Play(Bank &bank, float gain = 1.0f, ULONGLONG cooldownMs = 90) {
  if (!s_ready || !Config::AudioEnabled || bank.samples.empty())
    return false;
  const ULONGLONG now = GetTickCount64();
  if (bank.lastPlayedAt && now - bank.lastPlayedAt < cooldownMs)
    return true;
  IXAudio2SourceVoice *voice = AcquireVoice();
  if (!voice)
    return true;
  bank.lastPlayedAt = now;

  std::uniform_int_distribution<int> pick(0,
      static_cast<int>(bank.samples.size()) - 1);
  int index = pick(s_rng);
  if (bank.samples.size() > 1 && index == bank.lastIndex)
    index = (index + 1 + (pick(s_rng) % (bank.samples.size() - 1))) %
            static_cast<int>(bank.samples.size());
  bank.lastIndex = index;
  Sample &sample = bank.samples[index];

  const float spread =
      std::clamp(Config::AudioPitchRandomness, 0.0f, 0.18f);
  std::uniform_real_distribution<float> pitch(1.0f - spread, 1.0f + spread);
  std::uniform_real_distribution<float> level(0.92f, 1.0f);

  XAUDIO2_BUFFER buffer{};
  buffer.AudioBytes = static_cast<UINT32>(sample.pcm.size());
  buffer.pAudioData = sample.pcm.data();
  buffer.Flags = XAUDIO2_END_OF_STREAM;

  voice->Stop();
  voice->FlushSourceBuffers();
  voice->SetFrequencyRatio(pitch(s_rng));
  voice->SetVolume(std::clamp(gain * level(s_rng), 0.0f, 1.0f));
  if (FAILED(voice->SubmitSourceBuffer(&buffer)))
    return false;
  return SUCCEEDED(voice->Start());
}

std::filesystem::path ModuleDirectory(HMODULE module) {
  wchar_t path[MAX_PATH]{};
  const DWORD size = GetModuleFileNameW(module, path, MAX_PATH);
  if (!size || size >= MAX_PATH)
    return {};
  return std::filesystem::path(path).parent_path();
}

} // namespace

bool Initialize(HMODULE module) {
  if (s_ready)
    return true;

  const HRESULT hr = XAudio2Create(&s_audio, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr) || !s_audio) {
    LOG_ERROR(Audio, "XAudio2Create failed hr=0x%08X",
              static_cast<unsigned>(hr));
    return false;
  }
  if (FAILED(s_audio->CreateMasteringVoice(&s_master))) {
    LOG_ERROR(Audio, "CreateMasteringVoice failed");
    Shutdown();
    return false;
  }

  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 44100;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = 176400;

  for (auto &voice : s_voices) {
    if (FAILED(s_audio->CreateSourceVoice(&voice, &format, 0,
                                          XAUDIO2_DEFAULT_FREQ_RATIO))) {
      LOG_ERROR(Audio, "CreateSourceVoice failed");
      Shutdown();
      return false;
    }
  }

  const auto root = ModuleDirectory(module) / L"manual-trans" / L"audio";
  LoadBank(root, s_carShift, {L"car_shift_01.wav", L"car_shift_02.wav"});
  LoadBank(root, s_carSoft, {L"car_shift_soft_01.wav"});
  LoadBank(root, s_carPower, {L"car_shift_power_01.wav"});
  LoadBank(root, s_bikeUp,
           {L"bike_shift_up_01.wav", L"bike_shift_up_02.wav",
            L"bike_shift_up_03.wav"});
  LoadBank(root, s_bikeDown,
           {L"bike_shift_down_01.wav", L"bike_shift_down_02.wav",
            L"bike_shift_down_03.wav"});
  LoadBank(root, s_bikeUpSoft,
           {L"bike_shift_up_soft_01.wav", L"bike_shift_up_soft_02.wav"});
  LoadBank(root, s_bikeDownSoft,
           {L"bike_shift_down_soft_01.wav", L"bike_shift_down_soft_02.wav"});
  LoadBank(root, s_bikeError,
           {L"bike_shift_error_01.wav", L"bike_shift_error_02.wav"});
  LoadBank(root, s_parkApply,
           {L"parking_brake_apply_01.wav", L"parking_brake_apply_02.wav",
            L"parking_brake_apply_03.wav"});
  LoadBank(root, s_parkRelease,
           {L"parking_brake_release_01.wav", L"parking_brake_release_02.wav"});
  LoadBank(root, s_autoSelector, {L"automatic_park_01.wav"});

  s_ready = true;
  Update();
  LOG_INFO(Audio, "Audio engine ready root=%ls voices=%zu", root.c_str(),
           s_voices.size());
  return true;
}

void Shutdown() {
  s_ready = false;
  for (auto &voice : s_voices) {
    if (voice) {
      voice->DestroyVoice();
      voice = nullptr;
    }
  }
  if (s_master) {
    s_master->DestroyVoice();
    s_master = nullptr;
  }
  if (s_audio) {
    s_audio->Release();
    s_audio = nullptr;
  }
}

void Update() {
  if (s_master) {
    // 3 dB-ish bus headroom sesudah limiter per sample.
    s_master->SetVolume(
        std::clamp(Config::AudioMasterVolume, 0.0f, 1.0f) * 0.70f);
  }
}

bool IsReady() { return s_ready; }

bool PlayManualShift(Vehicle vehicle, bool upshift, bool powerShift,
                     bool softShift) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const bool bike = VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
                    VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model);
  if (bike) {
    Bank &bank = upshift ? (softShift ? s_bikeUpSoft : s_bikeUp)
                         : (softShift ? s_bikeDownSoft : s_bikeDown);
    return Play(bank, powerShift ? 1.0f : 0.92f);
  }
  if (powerShift && !s_carPower.samples.empty())
    return Play(s_carPower, 0.95f);
  if (softShift && !s_carSoft.samples.empty())
    return Play(s_carSoft, 0.88f);
  return Play(s_carShift, 0.90f);
}

bool PlayGearGrind(Vehicle vehicle) {
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model))
    return Play(s_bikeError, 0.86f);
  return false;
}

bool PlayParkingBrake(bool engaged) {
  return Play(engaged ? s_parkApply : s_parkRelease, 0.86f, 180);
}

bool PlayAutomaticShift(Vehicle vehicle, bool selectorMove) {
  (void)vehicle;
  return selectorMove && Play(s_autoSelector, 0.72f, 160);
}

} // namespace AudioEngine
