# Audio Sprint & Gear Logic Expansion

Dokumen ini berisi rencana implementasi penuh (blueprint) untuk integrasi Audio eksternal (menggunakan FMOD) beserta restrukturisasi *state* transmisi (untuk mendukung PRNDS pada mode Automatic).

## 1. Arsitektur Audio & Library
Kita akan menggunakan **FMOD Studio API** (Low-Level Core API) karena standar industri, ringan, dan mendukung Spatial 3D Audio, DSP Effects, serta low-latency mixing.

**Direktori Asset:**
Semua custom sound akan diletakkan di dalam folder `GTA V/manual-trans-audio/` agar tidak bercampur dengan file game bawaan. File idealnya berformat `.wav` (PCM) atau `.ogg` untuk menghindari overhead decoding CPU dari MP3.

## 2. Audio Trigger Cases (Event & Logic)

Sistem harus membedakan *class* kendaraan dan *driving state* untuk menentukan audio mana yang diputar.

### A. Manual Transmission (Car)
- **`car-shift-up.wav` / `car-shift-down.wav`**: Suara klik tuas mekanik. Dijalankan tiap kali perpindahan gigi sukses.
- **`car-shift-power.wav` (Clutch Dump / Power Shift)**: Suara hentakan berat (deg/thud). 
  - *Trigger Condition*: `Throttle > 0.8` && `RPM > 0.7` && `Clutch` dilepas (dari >0.8 ke <0.2) dalam waktu sangat singkat (< 100ms).
- **`car-shift-neutral.wav`**: Suara ditarik ke netral.
- **`car-clutch-grind.wav`**: Miss-shift (gear grind). Sudah ada logic di script, tinggal menambahkan *trigger* audionya.

### B. Automatic Transmission (PRNDS)
Kita perlu menambahkan *State Machine* baru untuk transmisi otomatis.
- **State `[P] Park`**: Memainkan `matic-park.wav`. Mengunci rem mekanik (Handbrake on, throttle off).
- **State `[R] Reverse`**: Memainkan `matic-shift.wav`.
- **State `[N] Neutral`**: Memainkan `matic-shift.wav`.
- **State `[D] Drive`**: Memainkan `matic-shift.wav`.
- **State `[S] Sport`**: Memainkan `matic-sport-click.wav`. Logic: Auto-upshift ditahan sampai RPM lebih tinggi (misal > 0.95 vs > 0.85 di mode D).

### C. Motorcycles (Bike)
Motor menggunakan sekuensial (diinjak/dicongkel), suara mekaniknya jauh lebih keras dan berbentuk *clunk* ketimbang *click* tuas mobil.
- *Check Condition*: `VEHICLE::IS_THIS_MODEL_A_BIKE(ENTITY::GET_ENTITY_MODEL(vehicle))`
- **`bike-shift-up.wav`**: Suara congkelan/injekan gigi naik.
- **`bike-shift-down.wav`**: Suara injekan gigi turun.

---

## 3. EDGE CASES & PENANGANAN (Advanced Implementations)

Berikut adalah *edge cases* yang sering luput dari mod audio standar, namun wajib kita implementasikan agar terasa *AAA/Native*.

### Edge Case 1: Audio Spam / Overlapping
- **Masalah**: Jika ban spin dan gigi otomatis naik-turun dengan cepat (atau user spam tombol), suara `shift.wav` akan tertumpuk puluhan kali dan menjadi sangat bising.
- **Solusi**: Terapkan **Audio Channel Cooldown** (misal 150ms). Jika sebuah channel masih memutar suara shift, *fade out* suara lama dalam 20ms sebelum memutar suara baru, atau abaikan input baru sampai yang lama selesai.

### Edge Case 2: First-Person (Cabin) vs Third-Person View
- **Masalah**: Suara mekanik oper gigi terdengar sama kencangnya dari luar mobil maupun dari dalam mobil.
- **Solusi**: Gunakan native `CAM::GET_CAM_VIEW_MODE_FOR_CONTEXT(0)`. Jika *view* adalah First-Person (di dalam kabin), kita nyalakan **Low-Pass Filter (LPF) DSP** di FMOD (memotong frekuensi tinggi / *muffle*) dan menurunkan volumenya 30%, sehingga terdengar redam dari dalam dashboard.

### Edge Case 3: Pintu / Kaca Jendela Pecah
- **Masalah**: Melanjutkan poin #2, jika pemain berada di First-Person tapi kaca depan pecah atau pintu hilang, suara tidak boleh teredam.
- **Solusi**: Cek status jendela menggunakan native `VEHICLE::ARE_ALL_VEHICLE_WINDOWS_INTACT(vehicle)`. Jika pecah, efek LPF dikurangi secara dinamis.

### Edge Case 4: 3D Spatial Audio & Jarak Kamera
- **Masalah**: Jika audio diset sebagai 2D, suaranya akan tetap maksimal biarpun kamera GTA V sedang di-zoom jauh (Cinematic cam).
- **Solusi**: Ikat emitter FMOD ke koordinat 3D mobil (`ENTITY::GET_ENTITY_COORDS`). Dengan begini, suara akan berpindah (panning) ke earphone kiri/kanan jika mobil berbelok relatif terhadap kamera.

### Edge Case 5: Slow Motion (Franklin Special Ability)
- **Masalah**: Saat Franklin mengaktifkan special ability (waktu melambat), audio eksternal biasanya tetap diputar dalam kecepatan normal (terdengar aneh).
- **Solusi**: Baca nilai `MISC::GET_TIME_SCALE()`. Kalikan nilai *pitch* (kecepatan play) FMOD dengan nilai time scale ini. Saat *slow-mo*, suara oper gigi akan otomatis melambat (deep pitch).

### Edge Case 6: Sinkronisasi Master Volume Game
- **Masalah**: Pemain mengecilkan volume SFX di Pause Menu GTA V, tapi suara gigi dari mod kita tetap kencang.
- **Solusi**: Gunakan native `AUDIO::GET_PROFILE_SETTING(300)` (Index 300 adalah SFX volume GTA V). Ambil persentasenya dan kalikan ke FMOD Master Group Volume.

### Edge Case 7: Tunnel Reverb (Gema Terowongan)
- **Masalah**: Suara dari FMOD terdengar datar di dalam terowongan yang menggema.
- **Solusi**: 
  1. Tembakkan `SHAPE_TEST_RAY_EXTRA_CAST` dari koordinat atap mobil ke arah `Z + 20.0f`.
  2. Jika *raycast* mendeteksi benturan (Hit), itu artinya ada atap/langit-langit di atas mobil.
  3. Aktifkan **FMOD Reverb DSP** secara gradual. Semakin dekat langit-langit, tingkat *Wet* (gema) reverb semakin besar. Matikan saat keluar terowongan.
