# Model drivetrain

## Netral

`Gear` dan `NextGear` memakai sentinel netral GTA. Clutch actuator diberi nilai
open dan engine bebas rev. Karena GTA tidak konsisten menaikkan RPM saat clutch
terbuka, kenaikan RPM dilengkapi memakai:

```text
ΔRPM = throttle × 2 × fDriveInertia × Δt
```

Drag, idle, audio dasar, dan limiter tetap berasal dari engine GTA. Field
throttle internal ikut ditulis hanya ketika drivetrain open supaya suara engine
selaras dengan RPM.

## Clutch

Pedal bernilai `0` saat dilepas dan `1` saat diinjak. Free play dan bite range
diubah dengan smoothstep menjadi engagement `0..1`.

- gear tetap terpilih ketika pedal diinjak;
- `fClutch` yang memutus torque;
- pelepasan pelan menaikkan torque capacity bertahap;
- pelepasan cepat menghasilkan perubahan capacity cepat;
- slip menambah heat dan heat tinggi mengurangi capacity.

Keyboard hanya punya dua state, jadi kecepatan release berasal dari
`ClutchRelease`. Pedal analog dibutuhkan untuk membedakan release pelan dan dump
clutch secara fisik pada satu profil yang sama.

## Load dan stall

Kecepatan minimum drivetrain dihitung dari idle RPM, `fDriveMaxFlatVel`, dan
rasio gear aktif. Stall progress hanya naik bila:

- clutch sudah menggigit kuat;
- RPM jatuh ke idle;
- kecepatan aktual masih di bawah kecepatan idle gear tersebut.

Throttle tidak langsung dipakai sebagai pengecualian. Bila mesin punya torsi
cukup, RPM native naik dan stall progress batal. Ini membuat kendaraan kuat bisa
berangkat tanpa clutch, sementara throttle kecil atau gear tinggi lebih mudah
bog/stall.

## TCS dan ABS

Keduanya membaca angular velocity, load, power, dan brake pressure dari
`CWheel`. Rolling radius dipelajari saat kendaraan rolling tanpa input besar.
Intervensi baru boleh terjadi setelah sedikitnya dua roda menghasilkan sample
valid.
