# Smart Bin IoT - 3 Tong Sampah Pintar

Sistem monitoring dan automasi tempat sampah pintar 3 kategori (**Organik**, **Anorganik**, dan **B3**) berbasis **ESP32**, **MQTT (shiftr.io)**, dan **Web Dashboard Modern Responsif (Laptop & HP)**.

---

## 🌟 Fitur Utama

- **Real-Time Telemetry**: Menampilkan kapasitas isi (%) dan jarak sampah (cm) setiap tong, jarak orang (cm), status servo, serta status penuh/normal.
- **Responsif di Semua Perangkat**:
  - **Laptop & Desktop**: Tampilan layar penuh dengan sidebar, grafik tren, 3 kartu tong sejajar, dan widget LED matriks.
  - **Smartphone / HP**: Mengadopsi navigasi bawah (*Bottom Navigation Bar*) modern seperti aplikasi mobile asli, kartu tong tersusun vertikal rapi, dan laci pengaturan menjadi *Bottom Sheet*.
- **Kendali Manual Servo**: Buka atau tutup tutup tong secara manual dengan sekali klik pada label servo di web dashboard via MQTT.
- **Offline Resilient**: ESP32 tetap berjalan 100% normal tanpa jeda atau sentakan servo meskipun koneksi Wi-Fi/Internet terputus.
- **Kalibrasi Matriks Tersimpan Permanen**: Pengaturan orientasi LED Matrix 96-kolom otomatis tersimpan ke memori internal Flash NVS ESP32 saat menekan tombol `'k'` di Serial Monitor (tidak akan hilang saat restart).
- **Gerakan Servo Cepat (~0.25 detik)**: Buka-tutup responsif dengan transisi redaman langkah halus.

---

## 🔌 Skema Pin Hardware ESP32

| Komponen | Pin ESP32 | Keterangan |
| :--- | :--- | :--- |
| **Matrix DIN** | GPIO 32 | Tersambung paralel ke semua panel |
| **Matrix CLK** | GPIO 33 | Tersambung paralel ke semua panel |
| **Matrix CS Kiri** | GPIO 23 | CS Panel 1 |
| **Matrix CS Tengah**| GPIO 16 | CS Panel 2 |
| **Matrix CS Kanan** | GPIO 17 | CS Panel 3 |
| **Sensor Luar (Orang)** | Trig: 2, 4 \| Echo: 34, 35 | Echo wajib divider 1k/2k ohm |
| **Sensor Dalam (Isi)** | Trig: 13, 14, 15 \| Echo: 36, 39, 19 | Echo wajib divider 1k/2k ohm |
| **Servo Motor (3x)** | GPIO 21, 22, 25 | Organik, Anorganik, B3 |
| **DFPlayer Mini** | RX: GPIO 27 \| TX: GPIO 26 | TX ESP32 ke RX DFPlayer via resistor 1k |

---

## 🚀 Panduan Deploy ke GitHub & Vercel

### 1. Upload ke GitHub
Buka terminal / PowerShell di folder proyek ini:

```bash
git init
git add .
git commit -m "Initial commit - Smart Bin IoT Dashboard & ESP32 MQTT"
git branch -M main
git remote add origin https://github.com/USERNAME_ANDA/NAMA_REPO_ANDA.git
git push -u origin main
```

### 2. Deploy ke Vercel (Gratis & Cepat)
1. Buka [vercel.com](https://vercel.com) dan login menggunakan akun GitHub Anda.
2. Klik **"Add New..."** lalu pilih **"Project"**.
3. Import repositori GitHub yang baru saja Anda buat.
4. Pada bagian **Framework Preset**, biarkan **"Other"**.
5. Klik **"Deploy"**.
6. Dalam hitungan detik, web dashboard Anda sudah online dan dapat diakses dari HP maupun laptop di mana saja melalui URL Vercel (misal: `https://smartbin-iot.vercel.app`).

---

## ⚙️ Konfigurasi MQTT (shiftr.io)
- **Broker**: `smartnose.cloud.shiftr.io`
- **Port Web**: `443` (WebSocket wss://)
- **Port ESP32**: `1883` (TCP MQTT)
- **Device ID Default**: `12345678` (dapat diubah di panel pengaturan dashboard atau URL query `?device=XXXX`)
