# Memory fields

Semua angka di bawah adalah displacement relatif terhadap object yang tertulis,
bukan alamat absolut.

| Field | Object | Resolver / relasi | Akses |
|---|---|---|---|
| Current RPM | `CVehicle` | pola engine | read-only saat runtime |
| Clutch actuator | `CVehicle` | displacement RPM `+0xC` | read/write |
| Engine throttle | `CVehicle` | belum tervalidasi di Enhanced | nonaktif |
| Throttle pedal | `CVehicle` | kelompok steering input `+0x10` | opsional read |
| Gear / NextGear | `CVehicle` | pola transmission | read/write |
| Gear ratios | `CVehicle` | inline `NextGear + 0xC`, atau pointer build lama | read |
| Handling pointer | `CVehicle` | pola handling | read |
| Drive inertia | `CHandlingData` | `0x54` | read |
| Initial drive force | `CHandlingData` | `0x60` | read |
| Drive max flat velocity | `CHandlingData` | `0x64` | read |
| Wheel array / count | `CVehicle` | pola wheel count | read |
| Angular velocity | `CWheel` | pola suspension group `+0xC` | read |
| Load / brake / power | `CWheel` | kelompok steering-angle wheel | read |

Offset INI dipisah per build GTA. Resolver memperkaya field opsional walau
cluster gear harus memakai fallback INI. Nilai `Clutch` lama dikoreksi ke
`RPM+0xC`; `Throttle` lama dijadikan nol. Core layout wajib punya `Gear`,
`NextGear`, `RPM`, dan `Clutch`; field opsional nol selalu no-op.

Kalibrasi gear Enhanced wajib mencocokkan `TopGear` dengan jumlah gear native
dan memvalidasi urutan rasio inline: reverse, gigi 1, lalu gigi 2. Heuristik
jarak build lama tidak lagi boleh memenangkan kandidat tanpa bukti rasio.

Jangan menambah write baru hanya karena sebuah float terlihat bergerak di log.
Field baru harus punya signature, relasi struct yang konsisten, range check, dan
jalur no-op ketika resolver gagal.

Runtime menulis clutch dengan semantik signed GTA: `1` terhubung, nilai negatif
hard-open. Setter RPM/throttle tidak dipanggil; RPM/audio/limiter wajib berasal
dari state engine GTA.
