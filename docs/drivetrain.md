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
- dump terdeteksi dari laju release pedal, bukan hanya posisi akhirnya;
- dump memberi shock feedback; hentakan fisik utamanya tetap berasal dari
  perubahan kapasitas clutch native;
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

Idle creep hanya diinjeksikan pada gear 1/reverse ketika brake dan throttle
dilepas. Pada tanjakan tidak ada hill-hold manual di gear: bila torque idle
kalah oleh beban, kendaraan boleh rollback dan mesin dapat stall.

## Manual shift edge cases

- Shift tanpa clutch tetap memilih gear; RPM mismatch menentukan clash,
  torque cut, getaran, dan wear.
- No-lift shift menyimpan shock sampai clutch menggigit kembali.
- Downshift dengan target RPM di atas limiter ditandai sebagai money shift dan
  mengurangi gearbox health.
- Reverse dari netral ditolak di atas batas kecepatan yang dikonfigurasi.
- Netral ke gear 2 atau upshift di RPM rendah tidak mematikan throttle. Rasio
  gear hanya menurunkan torque reserve sehingga mobil terasa berat atau stall
  bila torsi benar-benar tidak cukup.

## Automatic P-R-N-D-S

Shift Down menggerakkan selector `P → R → N → D → S`; Shift Up bergerak ke
arah sebaliknya.

- P membuka driveline dan mengunci parking brake.
- R memakai W sebagai throttle mundur dan S sebagai rem.
- N membuka driveline dan tetap mengizinkan free-rev.
- D melakukan upshift lebih awal dan downshift lebih rendah.
- S menahan RPM, lebih responsif melakukan kickdown, mempertahankan gear rendah
  saat braking, dan memakai torque multiplier terpisah.

Keluar dari P, memilih R, dan memilih arah drive dari N memerlukan brake bila
interlock aktif. P/R juga ditolak bila arah/kecepatan kendaraan belum aman.
Automatic memakai coupling torque-converter dan tidak menjalankan stall manual.

## Pedal dan rem

Gas+rem yang ditahan bersamaan memicu brake-throttle override setelah delay,
kecuali clutch manual sedang terbuka untuk heel-toe/rev-match atau launch
control sedang berada di launch window. ABS tetap fail-open bila telemetry roda
tidak valid. Temperatur rem tetap dihitung dari brake input dan road speed;
setelah ambang fade, tekanan maksimum berkurang sampai rem kembali dingin.

## TCS dan ABS

Keduanya membaca angular velocity, load, power, dan brake pressure dari
`CWheel`. Rolling radius dipelajari saat kendaraan rolling tanpa input besar.
Intervensi baru boleh terjadi setelah sedikitnya dua roda menghasilkan sample
valid.
