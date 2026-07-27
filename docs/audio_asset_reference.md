# Referensi aset audio

Mixer menerima WAV PCM stereo 44.1 kHz 16-bit. Simpan transient mekanikal
pendek, tanpa musik, ambience panjang, atau limiter keras dari editor. Peak
akhir yang aman sekitar -3 sampai -6 dBFS; limiter runtime tetap menjadi pagar
terakhir, bukan alat untuk memperbaiki rekaman clipping.

## Bank yang langsung dikenali

```text
automatic_selector_01.wav .. automatic_selector_04.wav

car_shift_01.wav .. car_shift_06.wav
car_shift_soft_01.wav .. car_shift_soft_04.wav
car_shift_power_01.wav .. car_shift_power_04.wav

bike_shift_up_01.wav .. bike_shift_up_06.wav
bike_shift_down_01.wav .. bike_shift_down_06.wav
bike_shift_up_soft_01.wav .. bike_shift_up_soft_04.wav
bike_shift_down_soft_01.wav .. bike_shift_down_soft_04.wav
bike_shift_error_01.wav .. bike_shift_error_04.wav

parking_brake_apply_01.wav .. parking_brake_apply_05.wav
parking_brake_release_01.wav .. parking_brake_release_04.wav

turbo_blowoff_01.wav .. turbo_blowoff_03.wav
turbo_flutter_01.wav .. turbo_flutter_03.wav
clutch_slip_01.wav .. clutch_slip_03.wav
transmission_clunk_01.wav .. transmission_clunk_03.wav
engine_lug_01.wav .. engine_lug_03.wav
abs_pulse_01.wav .. abs_pulse_02.wav
tcs_cut_01.wav .. tcs_cut_02.wav
launch_cut_01.wav .. launch_cut_03.wav
drivetrain_flex_01.wav .. drivetrain_flex_02.wav
```

`automatic_park_01.wav` masih dibaca sebagai nama legacy, tetapi nama baru
yang disarankan adalah `automatic_selector_XX.wav` karena event ini mewakili
gerakan tuas P-R-N-D-S-L, bukan perpindahan D1-D2-D3.

## Karakter yang dicari

- Selector: detent tuas pendek, halus, tanpa suara mesin.
- Car normal: klik/linkage bersih dengan body resonance tipis.
- Car soft: level lebih kecil dan transient lebih bulat untuk low-RPM shift.
- Car power: pukulan drivetrain pendek; jangan memasukkan rev engine panjang.
- Bike up/down: bunyi congkel dan injak dibedakan supaya arah shift terbaca.
- Bike error: ratchet/grind pendek khusus missed shift.
- Parking brake: apply punya beberapa klik ratchet, release lebih singkat.

Varian sebaiknya benar-benar berbeda pada transient, bukan sekadar file yang
sama dengan volume berbeda. Randomizer menghindari pengulangan varian terakhir
dan memberi variasi pitch/level kecil.

## Layer native dan layer opsional

Turbo spool/boost, exhaust pop, tyre slip, permukaan jalan, tunnel, dan angin
tetap memakai layer native GTA. Blow-off/flutter dan transient mekanikal lain
boleh memakai bank WAV di atas; bila bank tidak tersedia, event yang cocok
jatuh ke sound/exhaust native. Hindari WAV loop panjang agar tidak terjadi
phasing dengan audio kendaraan GTA.

Aset buatan sendiri, rekaman sendiri, atau aset dengan lisensi redistribusi
yang jelas paling aman untuk bundle open-source. Simpan catatan sumber dan
lisensi tiap sampel bila aset berasal dari pihak lain.
