#define NOMINMAX
#include "AudioEngine.h"

#include "../Core/Config.h"
#include "../Core/ModLogger.h"
#include "../Script/DrivingEventBus.h"
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
bool s_eventHandlersRegistered = false;

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
Bank s_turboBlowoff;
Bank s_turboFlutter;
Bank s_clutchSlip;
Bank s_transmissionClunk;
Bank s_engineLug;
Bank s_absPulse;
Bank s_tcsCut;
Bank s_launchCut;
Bank s_drivetrainFlex;
Vehicle s_nativePopVehicle = 0;
ULONGLONG s_nativePopUntil = 0;

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

void ReleaseBanks() {
  Bank *banks[] = {
      &s_carShift,         &s_carSoft,          &s_carPower,
      &s_bikeUp,           &s_bikeDown,         &s_bikeUpSoft,
      &s_bikeDownSoft,     &s_bikeError,        &s_parkApply,
      &s_parkRelease,      &s_autoSelector,     &s_turboBlowoff,
      &s_turboFlutter,     &s_clutchSlip,       &s_transmissionClunk,
      &s_engineLug,        &s_absPulse,         &s_tcsCut,
      &s_launchCut,        &s_drivetrainFlex,
  };
  for (Bank *bank : banks)
    *bank = Bank{};
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

bool Play(Bank &bank, float gain = 1.0f, ULONGLONG cooldownMs = 90,
          float pitchBias = 1.0f) {
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
  voice->SetFrequencyRatio(
      std::clamp(pitch(s_rng) * pitchBias, 0.72f, 1.35f));
  voice->SetVolume(std::clamp(gain * level(s_rng), 0.0f, 1.0f));
  if (FAILED(voice->SubmitSourceBuffer(&buffer)))
    return false;
  return SUCCEEDED(voice->Start());
}

void EnableNativePopWindow(Vehicle vehicle, ULONGLONG durationMs) {
  if (!Config::AudioEnabled || !Config::AudioNativeLayers || !vehicle ||
      !ENTITY::DOES_ENTITY_EXIST(vehicle))
    return;
  if (s_nativePopVehicle && s_nativePopVehicle != vehicle &&
      ENTITY::DOES_ENTITY_EXIST(s_nativePopVehicle))
    AUDIO::ENABLE_VEHICLE_EXHAUST_POPS(s_nativePopVehicle, FALSE);
  AUDIO::ENABLE_VEHICLE_EXHAUST_POPS(vehicle, TRUE);
  s_nativePopVehicle = vehicle;
  s_nativePopUntil =
      std::max(s_nativePopUntil, GetTickCount64() + durationMs);
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
    Shutdown();
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

  const auto root =
      ModuleDirectory(module) / L"melar-transmission" / L"audio";
  LoadBank(root, s_carShift,
           {L"car_shift_01.wav", L"car_shift_02.wav",
            L"car_shift_03.wav", L"car_shift_04.wav",
            L"car_shift_05.wav", L"car_shift_06.wav"});
  LoadBank(root, s_carSoft,
           {L"car_shift_soft_01.wav", L"car_shift_soft_02.wav",
            L"car_shift_soft_03.wav", L"car_shift_soft_04.wav"});
  LoadBank(root, s_carPower,
           {L"car_shift_power_01.wav", L"car_shift_power_02.wav",
            L"car_shift_power_03.wav", L"car_shift_power_04.wav"});
  LoadBank(root, s_bikeUp,
           {L"bike_shift_up_01.wav", L"bike_shift_up_02.wav",
            L"bike_shift_up_03.wav", L"bike_shift_up_04.wav",
            L"bike_shift_up_05.wav", L"bike_shift_up_06.wav"});
  LoadBank(root, s_bikeDown,
           {L"bike_shift_down_01.wav", L"bike_shift_down_02.wav",
            L"bike_shift_down_03.wav", L"bike_shift_down_04.wav",
            L"bike_shift_down_05.wav", L"bike_shift_down_06.wav"});
  LoadBank(root, s_bikeUpSoft,
           {L"bike_shift_up_soft_01.wav", L"bike_shift_up_soft_02.wav",
            L"bike_shift_up_soft_03.wav", L"bike_shift_up_soft_04.wav"});
  LoadBank(root, s_bikeDownSoft,
           {L"bike_shift_down_soft_01.wav", L"bike_shift_down_soft_02.wav",
            L"bike_shift_down_soft_03.wav", L"bike_shift_down_soft_04.wav"});
  LoadBank(root, s_bikeError,
           {L"bike_shift_error_01.wav", L"bike_shift_error_02.wav",
            L"bike_shift_error_03.wav", L"bike_shift_error_04.wav"});
  LoadBank(root, s_parkApply,
           {L"parking_brake_apply_01.wav", L"parking_brake_apply_02.wav",
            L"parking_brake_apply_03.wav", L"parking_brake_apply_04.wav",
            L"parking_brake_apply_05.wav"});
  LoadBank(root, s_parkRelease,
           {L"parking_brake_release_01.wav", L"parking_brake_release_02.wav",
            L"parking_brake_release_03.wav", L"parking_brake_release_04.wav"});
  LoadBank(root, s_autoSelector,
           {L"automatic_selector_01.wav", L"automatic_selector_02.wav",
            L"automatic_selector_03.wav", L"automatic_selector_04.wav",
            L"automatic_park_01.wav"});
  LoadBank(root, s_turboBlowoff,
           {L"turbo_blowoff_01.wav", L"turbo_blowoff_02.wav",
            L"turbo_blowoff_03.wav"});
  LoadBank(root, s_turboFlutter,
           {L"turbo_flutter_01.wav", L"turbo_flutter_02.wav",
            L"turbo_flutter_03.wav"});
  LoadBank(root, s_clutchSlip,
           {L"clutch_slip_01.wav", L"clutch_slip_02.wav",
            L"clutch_slip_03.wav"});
  LoadBank(root, s_transmissionClunk,
           {L"transmission_clunk_01.wav", L"transmission_clunk_02.wav",
            L"transmission_clunk_03.wav"});
  LoadBank(root, s_engineLug,
           {L"engine_lug_01.wav", L"engine_lug_02.wav",
            L"engine_lug_03.wav"});
  LoadBank(root, s_absPulse,
           {L"abs_pulse_01.wav", L"abs_pulse_02.wav"});
  LoadBank(root, s_tcsCut,
           {L"tcs_cut_01.wav", L"tcs_cut_02.wav"});
  LoadBank(root, s_launchCut,
           {L"launch_cut_01.wav", L"launch_cut_02.wav",
            L"launch_cut_03.wav"});
  LoadBank(root, s_drivetrainFlex,
           {L"drivetrain_flex_01.wav", L"drivetrain_flex_02.wav"});

  s_ready = true;
  if (!s_eventHandlersRegistered) {
    using Event = DrivingEventBus::Event;
    using EventData = DrivingEventBus::EventData;
    auto shiftHandler = [](bool upshift, const EventData &event) {
      if (!Config::AudioEnabled ||
          !Config::AudioTransmissionSounds)
        return;
      const ShiftCharacter character =
          event.severity >= 0.70f
              ? ShiftCharacter::Harsh
              : (event.severity <= 0.25f ? ShiftCharacter::Slow
                                         : ShiftCharacter::Normal);
      if (!PlayShift(event.vehicle, upshift, character, event.quickShift)) {
        AUDIO::PLAY_SOUND_FRONTEND(-1, "NAV_LEFT_RIGHT",
                                   "HUD_FRONTEND_DEFAULT_SOUNDSET", TRUE);
      }
    };
    DrivingEventBus::Subscribe(
        Event::GearShiftUp,
        [shiftHandler](const EventData &event) {
          shiftHandler(true, event);
        });
    DrivingEventBus::Subscribe(
        Event::GearShiftDown,
        [shiftHandler](const EventData &event) {
          shiftHandler(false, event);
        });
    DrivingEventBus::Subscribe(Event::GearGrind,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          if (PlayGearGrind(event.vehicle))
            return;
          const Hash model = ENTITY::GET_ENTITY_MODEL(event.vehicle);
          if (VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
              VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)) {
            AUDIO::PLAY_SOUND_FROM_ENTITY(
                -1, "NAV_UP_DOWN", event.vehicle,
                "HUD_FREEMODE_SOUNDSET", FALSE, 0);
          } else {
            AUDIO::PLAY_SOUND_FROM_ENTITY(
                -1, "BAR_OUT_OF_RANGE", event.vehicle,
                "HUD_MINIGAME_SOUNDSET", FALSE, 0);
          }
        });
    DrivingEventBus::Subscribe(Event::SelectorChanged,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          PlaySelector(event.vehicle);
        });
    DrivingEventBus::Subscribe(Event::ParkingBrakeEngaged,
        [](const EventData &) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          PlayParkingBrake(true);
        });
    DrivingEventBus::Subscribe(Event::ParkingBrakeReleased,
        [](const EventData &) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          PlayParkingBrake(false);
        });
    DrivingEventBus::Subscribe(Event::TurboBlowoff,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioTurboSounds)
            return;
          if (!Play(s_turboBlowoff, 0.90f, 130,
                    0.92f + event.severity * 0.14f) &&
              Config::AudioNativeLayers) {
            AUDIO::PLAY_SOUND_FROM_ENTITY(
                -1, "TURBO_BLOW_OFF", event.vehicle, "0", FALSE, 0);
          }
        });
    DrivingEventBus::Subscribe(Event::TurboFlutter,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioTurboSounds)
            return;
          if (!Play(s_turboFlutter, 0.82f, 110,
                    0.88f + event.severity * 0.18f))
            EnableNativePopWindow(event.vehicle, 95);
        });
    DrivingEventBus::Subscribe(Event::ClutchSlip,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioClutchSounds)
            return;
          if (!Play(s_clutchSlip, 0.68f, 420,
                    0.82f + event.severity * 0.12f) &&
              Config::AudioNativeLayers) {
            AUDIO::PLAY_SOUND_FROM_ENTITY(
                -1, "ENGINE_LUGGING", event.vehicle, "0", FALSE, 0);
          }
        });
    DrivingEventBus::Subscribe(Event::ClutchDump,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioClutchSounds)
            return;
          Play(s_transmissionClunk,
               0.48f + event.severity * 0.28f, 180,
               0.86f + event.severity * 0.10f);
        });
    DrivingEventBus::Subscribe(Event::ClutchOverheat,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioClutchSounds)
            return;
          Play(s_clutchSlip, 0.58f + event.severity * 0.20f,
               650, 0.78f);
        });
    DrivingEventBus::Subscribe(Event::TransmissionClunk,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          if (!Play(s_transmissionClunk,
                    0.55f + event.severity * 0.35f, 150, 0.94f) &&
              Config::AudioNativeLayers) {
            AUDIO::PLAY_SOUND_FROM_ENTITY(
                -1, "NAV_UP_DOWN", event.vehicle,
                "HUD_FREEMODE_SOUNDSET", FALSE, 0);
          }
        });
    DrivingEventBus::Subscribe(Event::EngineLug,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioEngineLoadSounds)
            return;
          if (!Play(s_engineLug, 0.58f + event.severity * 0.28f,
                    480, 0.78f + event.severity * 0.12f) &&
              Config::AudioNativeLayers) {
            AUDIO::PLAY_SOUND_FROM_ENTITY(
                -1, "ENGINE_LUGGING", event.vehicle, "0", FALSE, 0);
          }
        });
    DrivingEventBus::Subscribe(Event::EngineStall,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioEngineLoadSounds)
            return;
          Play(s_engineLug, 0.62f, 900, 0.74f);
          if (Config::AudioNativeLayers)
            EnableNativePopWindow(event.vehicle, 80);
        });
    DrivingEventBus::Subscribe(Event::MoneyShift,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          Play(s_transmissionClunk,
               0.72f + event.severity * 0.25f, 220, 0.78f);
        });
    DrivingEventBus::Subscribe(Event::ABSPulse,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioAssistSounds)
            return;
          if (!Play(s_absPulse, 0.42f + event.severity * 0.22f, 170) &&
              Config::AudioNativeLayers) {
            AUDIO::PLAY_SOUND_FRONTEND(
                -1, "NAV_LEFT_RIGHT",
                "HUD_FRONTEND_DEFAULT_SOUNDSET", TRUE);
          }
        });
    DrivingEventBus::Subscribe(Event::TCSCut,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioAssistSounds)
            return;
          if (!Play(s_tcsCut, 0.48f + event.severity * 0.28f, 180))
            EnableNativePopWindow(event.vehicle, 75);
        });
    DrivingEventBus::Subscribe(Event::ESCActivated,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioAssistSounds)
            return;
          Play(s_tcsCut, 0.38f + event.severity * 0.20f, 280, 0.92f);
        });
    DrivingEventBus::Subscribe(Event::LaunchControlCut,
        [](const EventData &event) {
          if (!Config::AudioEnabled || !Config::AudioAssistSounds)
            return;
          if (!Play(s_launchCut, 0.65f + event.severity * 0.30f,
                    115, 0.94f + event.severity * 0.10f))
            EnableNativePopWindow(event.vehicle, 115);
        });
    DrivingEventBus::Subscribe(Event::DrivetrainFlex,
        [](const EventData &event) {
          if (!Config::AudioEnabled ||
              !Config::AudioTransmissionSounds)
            return;
          Play(s_drivetrainFlex, 0.42f + event.severity * 0.22f,
               260, 0.88f);
        });
    s_eventHandlersRegistered = true;
  }
  Update();
  LOG_INFO(Audio, "Audio engine ready root=%ls voices=%zu", root.c_str(),
           s_voices.size());
  return true;
}

void Shutdown(bool restoreNativeLayer) {
  if (restoreNativeLayer && s_nativePopVehicle &&
      ENTITY::DOES_ENTITY_EXIST(s_nativePopVehicle))
    AUDIO::ENABLE_VEHICLE_EXHAUST_POPS(s_nativePopVehicle, FALSE);
  s_nativePopVehicle = 0;
  s_nativePopUntil = 0;
  s_ready = false;
  if (s_audio)
    s_audio->StopEngine();
  for (auto &voice : s_voices) {
    if (voice) {
      voice->Stop();
      voice->FlushSourceBuffers();
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
  ReleaseBanks();
  LOG_INFO(Audio, "Audio engine shutdown complete");
}

void Update() {
  if (s_nativePopVehicle && GetTickCount64() >= s_nativePopUntil) {
    if (ENTITY::DOES_ENTITY_EXIST(s_nativePopVehicle))
      AUDIO::ENABLE_VEHICLE_EXHAUST_POPS(s_nativePopVehicle, FALSE);
    s_nativePopVehicle = 0;
    s_nativePopUntil = 0;
  }
  if (s_master) {
    // 3 dB-ish bus headroom sesudah limiter per sample.
    s_master->SetVolume(
        std::clamp(Config::AudioMasterVolume, 0.0f, 1.0f) * 0.70f);
  }
}

bool IsReady() { return s_ready; }

bool PlayShift(Vehicle vehicle, bool upshift, ShiftCharacter character,
               bool quickshifter) {
  if (!Config::AudioEnabled || !Config::AudioTransmissionSounds)
    return false;
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  const bool bike = VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
                    VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model);
  const bool harsh = character == ShiftCharacter::Harsh;
  const bool slow = character == ShiftCharacter::Slow;

  if (harsh && Config::AudioNativeLayers) {
    EnableNativePopWindow(vehicle, quickshifter ? 90 : 140);
  }

  if (bike) {
    Bank &bank = upshift ? (slow ? s_bikeUpSoft : s_bikeUp)
                         : (slow ? s_bikeDownSoft : s_bikeDown);
    return Play(bank, harsh ? 1.0f : (slow ? 0.82f : 0.92f), 75,
                quickshifter ? 1.12f : (harsh ? 1.06f : 1.0f));
  }
  if (harsh && !s_carPower.samples.empty())
    return Play(s_carPower, 0.96f, 90, 1.02f);
  if (slow && !s_carSoft.samples.empty())
    return Play(s_carSoft, 0.88f);
  return Play(s_carShift, 0.90f);
}

bool PlayGearGrind(Vehicle vehicle) {
  if (!Config::AudioEnabled || !Config::AudioTransmissionSounds)
    return false;
  const Hash model = ENTITY::GET_ENTITY_MODEL(vehicle);
  if (VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
      VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model))
    return Play(s_bikeError, 0.86f);
  return false;
}

bool PlayParkingBrake(bool engaged) {
  if (!Config::AudioEnabled || !Config::AudioTransmissionSounds)
    return false;
  return Play(engaged ? s_parkApply : s_parkRelease, 0.86f, 180);
}

bool PlaySelector(Vehicle vehicle) {
  if (!Config::AudioEnabled || !Config::AudioTransmissionSounds)
    return false;
  (void)vehicle;
  return Play(s_autoSelector, 0.72f, 150);
}

} // namespace AudioEngine
