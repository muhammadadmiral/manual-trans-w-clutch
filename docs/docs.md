\# Dokumentasi ASI Plugin GTA V (PoC)

Dokumen ini menjelaskan struktur dasar dari kode *skeleton* ASI plugin untuk GTA V menggunakan ScriptHookV yang ada di file `main.cpp`.

## 1. Entry Point (`DllMain`)
Sebuah plugin `.asi` pada dasarnya adalah *Dynamic Link Library* (DLL) khusus platform Windows. 
* Ketika plugin dimuat oleh game (`DLL_PROCESS_ATTACH`), kita memanggil `DisableThreadLibraryCalls` untuk optimasi performa. Setelah itu, kita mendaftarkan fungsi *thread* khusus (*custom script*) kita ke game menggunakan fungsi `scriptRegister()` bawaan ScriptHookV.
* Ketika plugin dilepas (`DLL_PROCESS_DETACH`), kita membersihkan dan menghapus *thread* tersebut dari sistem dengan `scriptUnregister()`.

## 2. Main Loop (`ScriptMain`)
`ScriptMain` adalah fungsi utama yang berjalan secara terpisah dari *thread* utama game, di mana semua logika mod kita akan dieksekusi.
* `while (true)`: Sebuah *infinite loop* (perulangan abadi) yang terus berjalan selama *game* dimainkan.
* `scriptWait(0)`: **Sangat Krusial**. Fungsi ini memberikan kembali kendali proses (yield) kepada engine game pada setiap *tick* atau *frame*. Tanpa pemanggilan `scriptWait(0)` di dalam loop, *thread* game akan terblokir (*deadlock*), menyebabkan layar *freeze* (membeku) dan akhirnya game *crash*.

## 3. Proof of Concept (Cek Kendaraan Player)
Di dalam iterasi loop, modifikasi melakukan hal-hal berikut untuk memastikan landasan fitur awal berjalan:
1. `PLAYER::PLAYER_PED_ID()` dipanggil untuk mendapatkan entitas pemain (Franklin/Michael/Trevor atau MP Ped).
2. `PED::IS_PED_IN_ANY_VEHICLE(...)` melakukan pengecekan status secara *real-time* apakah karakter berada di dalam kendaraan.
3. `PED::GET_VEHICLE_PED_IS_IN(...)` mengambil "Handle" (ID unik) dari kendaraan yang sedang diduduki. Handle ini krusial dan nantinya menjadi kunci/landasan utama untuk mengekstraksi alamat memori asli (Entity Memory Pool / `CVehicle` pointer) melalui teknik *pattern scanning*.

## 4. Transmisi Manual & Logic (Phase 3)
- **Auto-Shift Blocker:** Kami menggunakan metode *Memory Overwriting* pada CVehicle untuk mencegah GTA V melakukan auto-shift. Secara spesifik, dengan memaksa nilai SetGear dan SetNextGear setiap frame (60 FPS), mesin fisika game (Handling) dipaksa untuk mempertahankan gear saat ini, memungkinkan RPM menyentuh batas limiter (Redline) tanpa berpindah gigi secara otomatis.
- **Vehicle Filtering (IsValidVehicle):** Transmisi manual tidak relevan untuk semua kendaraan. Oleh karena itu, kita memfilter:
  1. Kendaraan Non-Darat: Boat (14), Helicopter (15), Plane (16), Train (21).
  2. Kendaraan Elektrik & CVT: Kendaraan ini biasanya hanya memiliki 1 gigi penggerak di file handling.meta. Jika _GET_VEHICLE_MAX_DRIVE_GEAR_COUNT(veh) <= 1, maka mod akan mati secara otomatis.
- **Debounce Logic:** Digunakan pada input GetAsyncKeyState dengan variabel state (shiftUpPressed, shiftDownPressed) agar transmisi tidak lompat 2 gigi sekaligus jika tombol ditahan terlalu lama oleh pemain.
