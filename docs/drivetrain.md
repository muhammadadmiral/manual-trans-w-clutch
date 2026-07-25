# Model drivetrain

## Netral

`Gear` dan `NextGear` memakai sentinel netral GTA. W tetap masuk sebagai
throttle native, jadi engine bebas rev sementara drivetrain nol. Mod tidak
menulis RPM atau field throttle internal. Drag, inertia, limiter, dashboard,
dan audio engine berasal dari state mesin GTA yang sama.

## Clutch

Pedal bernilai `0` saat dilepas dan `1` saat diinjak. Free play dan bite range
diubah dengan smoothstep menjadi engagement `0..1`.

- logical gear tetap terpilih ketika pedal diinjak;
- engagement model membentuk torque capacity di bite range;
- pedal mentok juga menulis physical gear netral sebagai hard-disconnect,
  lalu logical gear masuk kembali saat pedal melewati bite point;
- pelepasan pelan menaikkan torque capacity bertahap;
- pelepasan cepat menghasilkan perubahan capacity cepat;
- dump terdeteksi dari laju release pedal, bukan hanya posisi akhirnya;
- dump memberi shock feedback; hentakan fisik mengikuti perubahan coupling;
- slip menambah heat dan heat tinggi mengurangi capacity.

Actuator clutch GTA bersifat signed: `1` terhubung penuh, sedangkan nilai
negatif dipakai untuk hard-open. Saat netral/kopling penuh, physical gear 1
menjadi carrier agar throttle GTA tetap bisa free-rev tanpa menulis RPM.
Sentinel `255` tidak dipakai karena Enhanced dapat menafsirkannya sebagai
permintaan auto-forward.

Keyboard hanya punya dua state, jadi kecepatan release berasal dari
`ClutchRelease`. Pedal analog dibutuhkan untuk membedakan release pelan dan dump
clutch secara fisik pada satu profil yang sama.

## Load dan stall

Kecepatan minimum drivetrain memakai idle RPM, `fDriveMaxFlatVel`, dan rasio
gear bila pointer handling valid. Bila pointer optional tidak tersedia, model
memakai `GET_VEHICLE_ESTIMATED_MAX_SPEED` dan
`GET_VEHICLE_ACCELERATION`, sehingga fallback tetap beda per kendaraan.
Stall progress hanya naik bila:

- clutch sudah menggigit kuat;
- RPM jatuh ke idle;
- kecepatan aktual masih di bawah kecepatan idle gear tersebut.

Cadangan torsi memperhitungkan throttle, leverage gear, brake load, dan pitch.
Kendaraan kuat bisa menarik lebih cepat; gear tinggi, tanjakan, atau rem yang
ditahan mempercepat bog/stall. Memasukkan gear dari netral tanpa clutch dan
tanpa throttle yang cukup menghasilkan stall request langsung.

Idle creep hanya diinjeksikan pada gear 1/reverse ketika brake dan throttle
dilepas. Pada tanjakan tidak ada hill-hold manual di gear: bila torque idle
kalah oleh beban, kendaraan boleh rollback dan mesin dapat stall.

## Manual shift edge cases

- Shift tanpa clutch tetap memilih gear; RPM mismatch menentukan clash,
  torque cut, getaran, dan wear.
- No-lift shift menyimpan shock sampai clutch menggigit kembali.
- Downshift dengan target RPM di atas limiter ditandai sebagai money shift,
  memberi shake, mengurangi gearbox health, dan merusak engine health native.
- Mod tidak mematikan engine untuk menyembunyikan over-rev. Gear native yang
  terpilih menentukan raungan; engine hanya mati lewat stall atau kerusakan.
- Reverse dari netral ditolak di atas batas kecepatan yang dikonfigurasi.
- Netral ke gear 2 atau upshift di RPM rendah tidak mematikan throttle. Rasio
  gear hanya menurunkan torque reserve sehingga mobil terasa berat atau stall
  bila torsi benar-benar tidak cukup.

## Automatic P-R-N-D-S-L2-L1

LShift menggerakkan selector `P → R → N → D → S → L2 → L1`; LCtrl bergerak
ke arah sebaliknya.

- P membuka driveline dan mengunci parking brake.
- R memakai W sebagai throttle mundur dan S sebagai rem.
- N membuka driveline dan tetap mengizinkan free-rev.
- D melakukan upshift lebih awal dan downshift lebih rendah.
- S menahan RPM, lebih responsif melakukan kickdown, mempertahankan gear rendah
  saat braking, dan memakai mapping pedal lebih agresif. Peak power tetap dari
  handling GTA.
- L2 membatasi automatic ke gear 1-2 dan L1 mengunci gear 1.

Keluar dari P, memilih R, dan memilih arah drive dari N memerlukan brake bila
interlock aktif. P/R juga ditolak bila arah/kecepatan kendaraan belum aman.
Automatic memakai coupling torque-converter dan tidak menjalankan stall manual.
Clutch input user diabaikan dan bar clutch disembunyikan.

Scooter `FAGGIO`, `FAGGIO2`, `FAGGIO3`, dan `PIZZABOY` langsung memakai D
sebagai CVT gas-rem. Motor lain tetap sequential, tetapi auto-clutch membuka
driveline sebentar pada shift. EV tetap automatic dan tidak menyediakan manual.

## Pedal, reverse, dan rem

W selalu dibaca sebagai throttle. Di R, W dipetakan ke reverse axis GTA. S
selalu menjadi brake: pada gear maju/netral, reverse axis diblok saat kendaraan
nyaris berhenti sehingga menahan S tidak bisa mengambil alih dan memundurkan
kendaraan. Echo reverse-axis dari frame sebelumnya dibuang dari pembacaan brake,
jadi injeksi W di R tidak menyalakan rem pada frame berikutnya.

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
