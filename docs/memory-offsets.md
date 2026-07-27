# Memory fields

Semua angka di bawah adalah displacement relatif terhadap object yang tertulis,
bukan alamat absolut.

| Field | Object | Resolver / relasi | Akses |
|---|---|---|---|
| Current RPM | `CVehicle` | pola/kalibrasi engine | read/write saat mode aktif |
| Clutch actuator | `CVehicle` | displacement RPM `+0xC` | read/write |
| Engine throttle | `CVehicle` | displacement RPM `+0x10` | read/write saat mode aktif |
| Throttle pedal | `CVehicle` | kelompok steering input `+0x10` | opsional read/write |
| Gear / NextGear | `CVehicle` | pola transmission | read/write |
| Gear ratios | `CVehicle` | inline `NextGear + 0xC`, atau pointer build lama | read |
| Handling pointer | `CVehicle` | pola handling | read |
| Drive inertia | `CHandlingData` | `0x54` | read |
| Clutch change rate upshift | `CHandlingData` | `0x58` | read |
| Clutch change rate downshift | `CHandlingData` | `0x5C` | read |
| Initial drive force | `CHandlingData` | `0x60` | read |
| Drive max flat velocity | `CHandlingData` | `0x64` | fallback read |
| Initial drive max flat velocity | `CHandlingData` | `0x68` | fallback read |
| Runtime drive force | `CTransmission` dalam `CVehicle` | `NextGear - 0x68` | read |
| Runtime drive max flat velocity | `CTransmission` dalam `CVehicle` | `NextGear - 0x60` | read |
| Wheel array / count | `CVehicle` | pola wheel count | read |
| Angular velocity | `CWheel` | pola suspension group `+0xC` | read |
| Load / brake / power | `CWheel` | kelompok steering-angle wheel | read |

Offset INI dipisah per build GTA. Resolver memperkaya field opsional walau
cluster gear harus memakai fallback INI. Nilai lama dikoreksi ke relasi cluster
`Clutch=RPM+0xC` dan `Throttle=RPM+0x10`. Core layout wajib punya `Gear`,
`NextGear`, `RPM`, dan `Clutch`; field opsional nol selalu no-op.

Kalibrasi gear Enhanced wajib mencocokkan `TopGear` dengan jumlah gear native
dan memvalidasi urutan rasio inline: reverse, gigi 1, lalu gigi 2. Heuristik
jarak build lama tidak lagi boleh memenangkan kandidat tanpa bukti rasio.

Jangan menambah write baru hanya karena sebuah float terlihat bergerak di log.
Field baru harus punya signature, relasi struct yang konsisten, range check, dan
jalur no-op ketika resolver gagal.

Runtime menulis clutch dengan semantik signed GTA: `1` terhubung, nilai negatif
hard-open. Jalur Enhanced punya dua patch kritis: shift-up + clutch dan
shift-down + clutch. Keduanya wajib unik dan dipasang atomik; kalau satu gagal,
takeover batal dan byte yang sempat ditulis direstore.

Writer `clutch low RPM` dan `throttle-lift` dipasang independen seperti jalur
Enhanced referensi. Keduanya opsional karena opcode lama dapat hilang di build
baru. Kalau low-RPM ditemukan unik, writer itu diarahkan dengan payload native
`C7 43 4C CD CC CC 3D`, bukan di-NOP. Kegagalan patch opsional tidak boleh
membatalkan dua patch kritis. Semua patch tetap menyimpan byte asli dan
direstore ketika mode Off, keluar kendaraan, atau unload.

RPM saat clutch tersambung dihitung dari road speed, runtime flat velocity, dan
rasio kendaraan aktif. RPM ini bukan pengganti kecepatan: wheel angular
velocity dan vehicle velocity tidak pernah ditulis.

## Runtime binding

`CVehicle`, handling, ratio table, dan pointer roda divalidasi ketika base
vehicle/layout berubah, lalu direvalidasi berkala setiap dua detik. Getter dan
setter yang jalan di frame loop hanya memakai cached memory-region bounds;
tidak ada `VirtualQuery`, `AOBScanner::IsReadable`, atau blok SEH per field.
Telemetry seluruh roda juga di-snapshot sekali per `VehicleData` frame lalu
dipakai bersama oleh engine model, TCS, dan ABS.

SEH tetap dibatasi ke scanner/kalibrasi/patch transaction yang bukan hot path.
Kalau binding periodik gagal, memory facade fail closed sampai binding valid
lagi dan tidak melakukan raw write.
