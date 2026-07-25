# Memory fields

Semua angka di bawah adalah displacement relatif terhadap object yang tertulis,
bukan alamat absolut.

| Field | Object | Resolver / relasi | Akses |
|---|---|---|---|
| Current RPM | `CVehicle` | pola engine | read-only saat runtime |
| Clutch | `CVehicle` | `RPM + 0xC` | read/write |
| Engine throttle | `CVehicle` | `RPM + 0x10` | read-only saat runtime |
| Throttle pedal | `CVehicle` | kelompok steering input `+0x10` | opsional read |
| Gear / NextGear | `CVehicle` | pola transmission | read/write |
| Gear ratios | `CVehicle` | pointer setelah cluster gear | read |
| Handling pointer | `CVehicle` | pola handling | read |
| Drive inertia | `CHandlingData` | `0x54` | read |
| Initial drive force | `CHandlingData` | `0x60` | read |
| Drive max flat velocity | `CHandlingData` | `0x64` | read |
| Wheel array / count | `CVehicle` | pola wheel count | read |
| Angular velocity | `CWheel` | pola suspension group `+0xC` | read |
| Load / brake / power | `CWheel` | kelompok steering-angle wheel | read |

Offset INI dipisah per build GTA. Resolver memperkaya field opsional walau
cluster gear harus memakai fallback INI. Nilai `Clutch=RPM+0x4` dari konfigurasi
lama dianggap stale dan dikoreksi saat load.

Jangan menambah write baru hanya karena sebuah float terlihat bergerak di log.
Field baru harus punya signature, relasi struct yang konsisten, range check, dan
jalur no-op ketika resolver gagal.

Wrapper setter RPM/throttle masih ada untuk kompatibilitas kalibrasi dan riset
offset, tetapi drivetrain runtime tidak memanggilnya. RPM/audio/limiter wajib
berasal dari state engine GTA.
