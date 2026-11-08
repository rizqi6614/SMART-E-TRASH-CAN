/*
 * =================================================================================
 * SMART BIN IOT - 3 TONG (ESP32) V2 WITH MQTT & WEB DASHBOARD INTEGRATION
 * 2 US luar (bersama) + 3 US dalam + 3 servo + DFPlayer + MAX7219 (96 kolom)
 * Terhubung ke Web Dashboard via MQTT shiftr.io (Non-Blocking / Offline Resilient)
 * Fitur: Kalibrasi Matrix tersimpan otomatis permanen ke NVS Flash saat klik 'k'
 * Gerakan Servo Cepat & Responsif (~0.25s)
 * =================================================================================
 *
 * MAX7219: 3 panel 4-in-1, DIN & CLK paralel, CS TERPISAH -> 1 layar 96 kolom
 *   ESP32 GPIO32 -> DIN semua panel (paralel)
 *   ESP32 GPIO33 -> CLK semua panel (paralel)
 *   ESP32 GPIO23 -> CS panel KIRI
 *   ESP32 GPIO16 -> CS panel TENGAH
 *   ESP32 GPIO17 -> CS panel KANAN
 *   VCC & GND semua panel disambung bersama (DOUT tidak dipakai)
 *
 * DFPLAYER MINI:
 *   ESP32 GPIO27 (RX) <- TX DFPlayer Mini
 *   ESP32 GPIO26 (TX) -> RX DFPlayer Mini (via resistor 1k ohm)
 *   File SD Card (FAT32, langsung di root folder):
 *     0001.mp3 = "Terima kasih"
 *     0002.mp3 = "Maaf, tong penuh"
 *     0003.mp3 = "Selamat datang" (opsional)
 *
 * SENSOR & SERVO:
 *   Sensor Luar (Orang): Trig {2, 4}, Echo {34, 35} (via divider 1k/2k)
 *   Sensor Dalam (Isi) : Trig {13, 14, 15}, Echo {36, 39, 19} (via divider 1k/2k)
 *   Servo              : GPIO {21, 22, 25} (Organik, Anorganik, B3)
 *
 * MQTT TOPICS (Broker: smartnose.cloud.shiftr.io / Port 1883):
 *   Prefix: smartbin/<DEVICE_ID>/...
 *   - smartbin/12345678/status      -> "online" / "offline"
 *   - smartbin/12345678/rssi        -> Sinyal WiFi (dBm)
 *   - smartbin/12345678/tong1/isi   -> Jarak sampah Tong 1 (cm)
 *   - smartbin/12345678/tong1/orang -> Jarak orang sensor luar (cm)
 *   - smartbin/12345678/tong1/servo -> "TERBUKA" / "TERTUTUP"
 *   - smartbin/12345678/tong1/status-> "NORMAL" / "PENUH"
 *   - smartbin/12345678/tong2/...   -> Data Tong 2 (Anorganik)
 *   - smartbin/12345678/tong3/...   -> Data Tong 3 (B3)
 *   - smartbin/12345678/led_system  -> "HIJAU (Normal)" / "MERAH (Penuh)"
 *   - smartbin/12345678/matrix_text -> Teks scrolling running di Matrix
 *   - smartbin/12345678/dfplayer    -> Status audio DFPlayer
 *
 * KALIBRASI MATRIX (Serial Monitor 115200, kirim 1 huruf):
 *   k = simpan setting permanen & beralih mode teks / garis
 *   1 / 2 / 3 = balik arah kolom panel kiri / tengah / kanan
 *   x = tukar posisi panel kiri <-> tengah
 *   y = tukar posisi panel tengah <-> kanan
 *   r = balik baris (atas-bawah)
 *   h = ganti tipe hardware (FC16 -> GENERIC -> PAROLA -> ICSTATION)
 *   ? = tampilkan pengaturan saat ini
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <ESP32Servo.h>
#include <MD_MAX72xx.h>
#include <Preferences.h>   // Penyimpanan permanen NVS Flash ESP32

// ================= PENGATURAN WI-FI & MQTT =================
// Silakan sesuaikan nama WiFi dan Password di bawah ini:
const char *WIFI_SSID    = "NAMA_WIFI_ANDA";      // Masukkan SSID Wi-Fi
const char *WIFI_PASS    = "PASSWORD_WIFI_ANDA";  // Masukkan Password Wi-Fi

// Broker MQTT shiftr.io sesuai konfigurasi Dashboard (app.js)
const char *MQTT_BROKER  = "smartnose.cloud.shiftr.io";
const int   MQTT_PORT    = 1883;
const char *MQTT_USER    = "smartnose";
const char *MQTT_PASS    = "sPMWfLIuQ2JT72FV";
const char *DEVICE_ID    = "12345678";   // Sesuai Serial Number default di dashboard

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

unsigned long tCobaWiFi = 0;
unsigned long tCobaMQTT = 0;
unsigned long tKirimTelemetry = 0;
const unsigned long INTERVAL_TELEMETRY = 1500; // Kirim telemetry berkala tiap 1.5 detik
String statusDfplayer = "Standby";
unsigned long tMulaiPutar = 0;

// Forward declarations
void publishTelemetry();
void gerakServo(int i, int pos);
void kirimMqtt(const char *subtopic, const char *payload, bool retain = true);
void simpanPengaturanMatrix();
void muatPengaturanMatrix();

// ================= MATRIX =================
#define JUMLAH_PANEL 3
#define MATRIX_PER_PANEL 4                          // 1 panel = 4 matriks 8x8
#define KOLOM_PANEL (MATRIX_PER_PANEL * 8)          // 32
#define LEBAR_LAYAR (JUMLAH_PANEL * KOLOM_PANEL)    // 96
#define DATA_PIN 32
#define CLK_PIN 33
#define MATRIX_INTENSITY 3                          // 0-15
#define MATRIX_CHAR_SPACE 1                         // jarak antar huruf (kolom)

// Urutan CS: panel paling KIRI -> paling KANAN (sesuai posisi fisik)
const uint8_t PIN_CS_MATRIX[JUMLAH_PANEL] = {23, 16, 17};
MD_MAX72XX *mx[JUMLAH_PANEL] = {nullptr, nullptr, nullptr};

// ===== KALIBRASI MATRIX (Tersimpan otomatis permanen ke NVS Flash) =====
bool modeKalibrasi = false;                     // default false jika sudah tersimpan; kirim 'k' utk kalibrasi
bool balikBaris = false;                        // true jika huruf terbalik atas-bawah
bool BALIK_KOLOM_PANEL[JUMLAH_PANEL] = {true, true, true};
uint8_t urutanPanel[JUMLAH_PANEL] = {0, 1, 2};  // posisi layar -> indeks panel (CS)
uint8_t hwIdx = 0;
const MD_MAX72XX::moduleType_t HW_LIST[4] = {
  MD_MAX72XX::FC16_HW, MD_MAX72XX::GENERIC_HW,
  MD_MAX72XX::PAROLA_HW, MD_MAX72XX::ICSTATION_HW
};
const char *HW_NAMA[4] = {"FC16_HW", "GENERIC_HW", "PAROLA_HW", "ICSTATION_HW"};

// Simpan konfigurasi kalibrasi matriks ke Flash ESP32 agar tidak berubah setelah restart
void simpanPengaturanMatrix() {
  prefs.begin("mat_cfg", false);
  prefs.putBool("cal", modeKalibrasi);
  prefs.putBool("row", balikBaris);
  prefs.putBool("c0", BALIK_KOLOM_PANEL[0]);
  prefs.putBool("c1", BALIK_KOLOM_PANEL[1]);
  prefs.putBool("c2", BALIK_KOLOM_PANEL[2]);
  prefs.putUChar("p0", urutanPanel[0]);
  prefs.putUChar("p1", urutanPanel[1]);
  prefs.putUChar("p2", urutanPanel[2]);
  prefs.putUChar("hw", hwIdx);
  prefs.putBool("saved", true);
  prefs.end();
  Serial.println("[NVS] Pengaturan matrix berhasil disimpan permanen ke memori Flash ESP32!");
}

// Muat konfigurasi kalibrasi matriks dari Flash ESP32
void muatPengaturanMatrix() {
  prefs.begin("mat_cfg", true);
  if (prefs.getBool("saved", false)) {
    modeKalibrasi = prefs.getBool("cal", false);
    balikBaris = prefs.getBool("row", false);
    BALIK_KOLOM_PANEL[0] = prefs.getBool("c0", true);
    BALIK_KOLOM_PANEL[1] = prefs.getBool("c1", true);
    BALIK_KOLOM_PANEL[2] = prefs.getBool("c2", true);
    urutanPanel[0] = prefs.getUChar("p0", 0);
    urutanPanel[1] = prefs.getUChar("p1", 1);
    urutanPanel[2] = prefs.getUChar("p2", 2);
    hwIdx = prefs.getUChar("hw", 0);
    if (hwIdx >= 4) hwIdx = 0;
    Serial.println("[NVS] Pengaturan matrix berhasil dimuat dari Flash!");
  } else {
    Serial.println("[NVS] Belum ada setting tersimpan di Flash, memakai nilai standar.");
  }
  prefs.end();
}

uint8_t balikBit(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

// Tulis 1 kolom global (0..95) ke panel & kolom lokal yang benar
void tulisKolom(uint16_t x, uint8_t val) {
  uint8_t posisi = x / KOLOM_PANEL;
  uint8_t lokal = x % KOLOM_PANEL;
  uint8_t c = BALIK_KOLOM_PANEL[posisi] ? (KOLOM_PANEL - 1 - lokal) : lokal;
  if (balikBaris) val = balikBit(val);
  mx[urutanPanel[posisi]]->setColumn(c, val);
}

// ================= PIN =================
#define JUMLAH_TONG 3
#define JUMLAH_LUAR 2

const int PIN_TRIG_LUAR[JUMLAH_LUAR] = {2, 4};
const int PIN_ECHO_LUAR[JUMLAH_LUAR] = {34, 35};   // via divider 1k/2k
const int PIN_TRIG_DALAM[JUMLAH_TONG] = {13, 14, 15};
const int PIN_ECHO_DALAM[JUMLAH_TONG] = {36, 39, 19}; // via divider 1k/2k
const int PIN_SERVO[JUMLAH_TONG] = {21, 22, 25};
const char *NAMA_TONG[JUMLAH_TONG] = {"ORGANIK", "ANORGANIK", "B3"};

// ================= DFPLAYER =================
#define PIN_MP3_RX 27  // RX ESP32  <- TX DFPlayer
#define PIN_MP3_TX 26  // TX ESP32  -> RX DFPlayer (via resistor 1k)

HardwareSerial mp3Serial(2);   // UART2
DFRobotDFPlayerMini player;
bool mp3Siap = false;

Servo myservo[JUMLAH_TONG];

// ================= PARAMETER =================
const int BATAS_BUKA = 35;    // cm, sensor luar
const int BATAS_PENUH = 38;   // cm, sensor dalam
const int BATAS_KOSONG = 40;  // cm, sensor dalam (histeresis)
const int POSISI_TUTUP = 0;
const int POSISI_BUKA = 190;

const unsigned long TUNDA_TUTUP = 3000;        // ms setelah tangan pergi
const unsigned long MIN_BUKA = 3500;           // ms minimal terbuka
const unsigned long MAX_BUKA = 20000;          // ms paksa tutup (pengaman)
const unsigned long JEDA_SETELAH_TUTUP = 4500; // ms sebelum cek isi / buka lagi
const unsigned long COOLDOWN_PENUH = 8000;     // ms antar suara "penuh"
const unsigned long INTERVAL_SENSOR = 30;      // ms antar pembacaan (1 sensor/tick)

const int VOLUME_MP3 = 30;                  // 0-30
const bool SUARA_SELAMAT_DATANG = true;     // track 0003
const bool SERVO_DETACH = false;            // true = lepas servo setelah gerak

// ================= STATE =================
bool terbuka = false;
bool sudahPenuh[JUMLAH_TONG] = {false, false, false};
unsigned long waktuBuka = 0, waktuTerakhirOrang = 0, waktuTutup = 0;
unsigned long waktuSuaraPenuh = 0;

int jarakLuar[JUMLAH_LUAR] = {999, 999};
int jarakDalam[JUMLAH_TONG] = {999, 999, 999};
int cntOrang[JUMLAH_LUAR] = {0, 0};
int cntPenuh[JUMLAH_TONG] = {0, 0, 0};
int cntKosong[JUMLAH_TONG] = {0, 0, 0};

unsigned long servoGerakAt[JUMLAH_TONG] = {0, 0, 0};
bool servoAktif[JUMLAH_TONG] = {false, false, false};

// matrix
char matrixBuf[64] = "AYO BUANG SAMPAH";
bool msgAktif = false;
bool idleDirty = true;
unsigned long msgSampai = 0;

uint8_t kolomBuf[900];          // teks yang sudah diubah jadi deretan kolom
uint16_t jumlahKolom = 0;
uint16_t offsetGeser = 0;
uint16_t kecepatanGeser = 45;   // ms per geser (besar = lambat)
unsigned long tGeser = 0;

// scheduler sensor
unsigned long tSensor = 0;
uint8_t tick = 0, idxLuar = 0, idxDalam = 0;

// ================= SENSOR =================
int bacaUltrasonik(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(3);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 12000); // ~2 m
  if (dur == 0) return 999;
  return (int)(dur * 0.0343 / 2.0);
}

// ================= SERVO (SMOOTH & CEPAT ~0.25 DETIK) =================
// SERVO_STEP = 6 derajat tiap 8 ms -> Buka/tutup penuh 190 derajat hanya ~0.25 detik
const int SERVO_STEP = 6;                 // derajat per langkah (lebih besar = lebih cepat)
const unsigned long SERVO_INTERVAL = 8;   // ms per langkah (lebih kecil = lebih cepat)

int servoPosSekarang[JUMLAH_TONG] = {POSISI_TUTUP, POSISI_TUTUP, POSISI_TUTUP};
int servoPosTarget[JUMLAH_TONG]   = {POSISI_TUTUP, POSISI_TUTUP, POSISI_TUTUP};
unsigned long servoLangkahAt[JUMLAH_TONG] = {0, 0, 0};

void gerakServo(int i, int pos) {
  if (!myservo[i].attached()) {
    myservo[i].attach(PIN_SERVO[i], 500, 2400);
    myservo[i].write(servoPosSekarang[i]);
  }
  servoPosTarget[i] = pos;
  servoAktif[i] = true;
  servoGerakAt[i] = millis();
}

void updateServo() {
  unsigned long now = millis();
  for (int i = 0; i < JUMLAH_TONG; i++) {
    if (servoPosSekarang[i] != servoPosTarget[i]) {
      if (now - servoLangkahAt[i] >= SERVO_INTERVAL) {
        servoLangkahAt[i] = now;
        int selisih = servoPosTarget[i] - servoPosSekarang[i];
        if (abs(selisih) <= SERVO_STEP) servoPosSekarang[i] = servoPosTarget[i];
        else servoPosSekarang[i] += (selisih > 0) ? SERVO_STEP : -SERVO_STEP;
        myservo[i].write(servoPosSekarang[i]);
        servoGerakAt[i] = now;
      }
    } else if (SERVO_DETACH && servoAktif[i] && now - servoGerakAt[i] > 1000) {
      myservo[i].detach();
      servoAktif[i] = false;
    }
  }
}

// ================= SUARA =================
void putarSuara(int track) {
  if (track == 1) statusDfplayer = "Track 1: Terima Kasih";
  else if (track == 2) statusDfplayer = "Track 2: Maaf Penuh";
  else if (track == 3) statusDfplayer = "Track 3: Selamat Datang";
  tMulaiPutar = millis();
  kirimMqtt("dfplayer", statusDfplayer.c_str());

  if (!mp3Siap) return;
  player.play(track); // memutar 000N.mp3
}

void inisialisasiDFPlayer() {
  mp3Serial.begin(9600, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
  delay(1000); // beri waktu DFPlayer boot

  Serial.println("Menghubungkan ke DFPlayer...");
  if (!player.begin(mp3Serial, /*isACK=*/true, /*doReset=*/true)) {
    Serial.println("[WARN] DFPlayer gagal! Cek wiring, resistor 1k & SD card. Sistem lanjut tanpa suara.");
    mp3Siap = false;
    return;
  }

  Serial.println("[OK] DFPlayer OK");
  mp3Siap = true;
  player.setTimeOut(500);
  player.outputDevice(DFPLAYER_DEVICE_SD);
  player.volume(VOLUME_MP3);
  player.EQ(DFPLAYER_EQ_NORMAL);
}

void updateDFPlayer() {
  // Reset otomatis ke Standby jika audio selesai atau timeout 5 detik
  if (statusDfplayer != "Standby" && millis() - tMulaiPutar > 5000) {
    statusDfplayer = "Standby";
    kirimMqtt("dfplayer", statusDfplayer.c_str());
  }

  if (!mp3Siap) return;
  if (player.available()) {
    uint8_t type = player.readType();
    int value = player.read();
    Serial.printf("[MP3] Event type=%d value=%d\n", type, value);
    // DFPlayer selesai memutar audio (type == 512 / DFPlayerPlayFinished)
    if (type == 512 || type == DFPlayerPlayFinished) {
      statusDfplayer = "Standby";
      kirimMqtt("dfplayer", statusDfplayer.c_str());
    }
  }
}

// ================= MATRIX (3 PANEL, CS TERPISAH) =================
void inisialisasiMatrix() {
  for (uint8_t p = 0; p < JUMLAH_PANEL; p++) {
    if (mx[p]) { delete mx[p]; mx[p] = nullptr; }
    mx[p] = new MD_MAX72XX(HW_LIST[hwIdx], DATA_PIN, CLK_PIN, PIN_CS_MATRIX[p], MATRIX_PER_PANEL);
    mx[p]->begin();
    mx[p]->control(MD_MAX72XX::INTENSITY, MATRIX_INTENSITY);
    mx[p]->clear();
  }
}

// Ubah teks jadi deretan kolom, dengan ruang kosong di awal & akhir
void siapkanKolom(const char *t) {
  jumlahKolom = 0;
  for (uint16_t i = 0; i < LEBAR_LAYAR; i++) kolomBuf[jumlahKolom++] = 0;

  uint8_t buf[12];
  for (const char *p = t; *p; p++) {
    if (jumlahKolom + 12 + MATRIX_CHAR_SPACE + LEBAR_LAYAR > sizeof(kolomBuf)) break;
    uint8_t w = mx[0]->getChar((uint8_t)*p, sizeof(buf), buf);
    for (uint8_t i = 0; i < w; i++) kolomBuf[jumlahKolom++] = buf[i];
    for (uint8_t s = 0; s < MATRIX_CHAR_SPACE; s++) kolomBuf[jumlahKolom++] = 0;
  }

  for (uint16_t i = 0; i < LEBAR_LAYAR; i++) kolomBuf[jumlahKolom++] = 0;
}

// Gambar satu frame ke ketiga panel
void gambarFrame(uint16_t offset) {
  for (uint8_t p = 0; p < JUMLAH_PANEL; p++)
    mx[p]->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  for (uint16_t x = 0; x < LEBAR_LAYAR; x++)
    tulisKolom(x, kolomBuf[offset + x]);

  for (uint8_t p = 0; p < JUMLAH_PANEL; p++)
    mx[p]->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

// Garis vertikal menyapu dari kolom 0 sampai 95 (untuk kalibrasi)
void kalibrasi() {
  static uint16_t pos = 0;
  static unsigned long t = 0;
  if (millis() - t < 60) return;
  t = millis();

  for (uint8_t p = 0; p < JUMLAH_PANEL; p++)
    mx[p]->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  for (uint16_t x = 0; x < LEBAR_LAYAR; x++)
    tulisKolom(x, (x == pos) ? 0xFF : 0x00);
  for (uint8_t p = 0; p < JUMLAH_PANEL; p++)
    mx[p]->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  pos = (pos + 1) % LEBAR_LAYAR;
}

void cetakStatusMatrix() {
  Serial.printf("[CFG] kalibrasi=%d balikBaris=%d BALIK_KOLOM_PANEL={%d,%d,%d} urutanPanel={%d,%d,%d} HW=%s\n",
                modeKalibrasi, balikBaris,
                BALIK_KOLOM_PANEL[0], BALIK_KOLOM_PANEL[1], BALIK_KOLOM_PANEL[2],
                urutanPanel[0], urutanPanel[1], urutanPanel[2], HW_NAMA[hwIdx]);
}

// Perintah kalibrasi lewat Serial Monitor
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    bool ubah = true;
    switch (c) {
      case 'k': 
        modeKalibrasi = !modeKalibrasi; 
        simpanPengaturanMatrix(); // Disimpan otomatis saat klik 'k'
        break;
      case '1': BALIK_KOLOM_PANEL[0] = !BALIK_KOLOM_PANEL[0]; simpanPengaturanMatrix(); break;
      case '2': BALIK_KOLOM_PANEL[1] = !BALIK_KOLOM_PANEL[1]; simpanPengaturanMatrix(); break;
      case '3': BALIK_KOLOM_PANEL[2] = !BALIK_KOLOM_PANEL[2]; simpanPengaturanMatrix(); break;
      case 'r': balikBaris = !balikBaris; simpanPengaturanMatrix(); break;
      case 'x': { uint8_t t = urutanPanel[0]; urutanPanel[0] = urutanPanel[1]; urutanPanel[1] = t; simpanPengaturanMatrix(); } break;
      case 'y': { uint8_t t = urutanPanel[1]; urutanPanel[1] = urutanPanel[2]; urutanPanel[2] = t; simpanPengaturanMatrix(); } break;
      case 'h': hwIdx = (hwIdx + 1) % 4; inisialisasiMatrix(); simpanPengaturanMatrix(); break;
      case '?': break;
      default: ubah = false; break;
    }
    if (ubah) cetakStatusMatrix();
  }
}

// Running text melintasi ketiga panel
void tampilScroll(const char *t, uint16_t speed = 35) {
  strncpy(matrixBuf, t, sizeof(matrixBuf) - 1);
  matrixBuf[sizeof(matrixBuf) - 1] = 0;

  siapkanKolom(matrixBuf);
  offsetGeser = 0;
  kecepatanGeser = speed;
  tGeser = millis();
  if (!modeKalibrasi) gambarFrame(0);

  // Kirim update teks matrix ke Dashboard
  kirimMqtt("matrix_text", matrixBuf);
}

void pesanSementara(const char *t, unsigned long durasi) {
  tampilScroll(t);
  msgAktif = true;
  msgSampai = millis() + durasi;
}

void bangunIdle() {
  String s = "";
  for (int i = 0; i < JUMLAH_TONG; i++) {
    if (sudahPenuh[i]) {
      if (s.length()) s += "  -  ";
      s += "TONG ";
      s += NAMA_TONG[i];
      s += " PENUH";
    }
  }
  if (s.length() == 0) s = "AYO BUANG SAMPAH";
  tampilScroll(s.c_str(), 45);
}

void updateMatrix() {
  if (modeKalibrasi) { kalibrasi(); return; }

  unsigned long now = millis();

  if (msgAktif && (long)(now - msgSampai) >= 0) {
    msgAktif = false;
    idleDirty = true;
  }
  if (!msgAktif && idleDirty) {
    bangunIdle();
    idleDirty = false;
  }

  if (now - tGeser < kecepatanGeser) return;
  tGeser = now;

  gambarFrame(offsetGeser);
  offsetGeser++;
  if (offsetGeser > jumlahKolom - LEBAR_LAYAR) offsetGeser = 0;  // ulangi scroll
}

// ================= MQTT TELEMETRY HELPER =================
void kirimMqtt(const char *subtopic, const char *payload, bool retain) {
  if (!mqttClient.connected()) return;
  char topikLengkap[96];
  snprintf(topikLengkap, sizeof(topikLengkap), "smartbin/%s/%s", DEVICE_ID, subtopic);
  mqttClient.publish(topikLengkap, payload, retain);
}

void publishTelemetry() {
  if (!mqttClient.connected()) return;

  // 1. Status ESP32 & WiFi RSSI
  kirimMqtt("status", "online");
  kirimMqtt("rssi", String(WiFi.RSSI()).c_str());

  // 2. Tong 1 (Organik)
  kirimMqtt("tong1/isi", String(jarakDalam[0]).c_str());
  kirimMqtt("tong1/orang", String(jarakLuar[0]).c_str());
  kirimMqtt("tong1/servo", (servoPosTarget[0] == POSISI_BUKA) ? "TERBUKA" : "TERTUTUP");
  kirimMqtt("tong1/status", sudahPenuh[0] ? "PENUH" : "NORMAL");

  // 3. Tong 2 (Anorganik)
  kirimMqtt("tong2/isi", String(jarakDalam[1]).c_str());
  kirimMqtt("tong2/orang", String(jarakLuar[1]).c_str());
  kirimMqtt("tong2/servo", (servoPosTarget[1] == POSISI_BUKA) ? "TERBUKA" : "TERTUTUP");
  kirimMqtt("tong2/status", sudahPenuh[1] ? "PENUH" : "NORMAL");

  // 4. Tong 3 (B3)
  kirimMqtt("tong3/isi", String(jarakDalam[2]).c_str());
  int orangTerdekat = min(jarakLuar[0], jarakLuar[1]);
  kirimMqtt("tong3/orang", String(orangTerdekat).c_str());
  kirimMqtt("tong3/servo", (servoPosTarget[2] == POSISI_BUKA) ? "TERBUKA" : "TERTUTUP");
  kirimMqtt("tong3/status", sudahPenuh[2] ? "PENUH" : "NORMAL");

  // 5. Status Indikator Sistem & DFPlayer
  bool adaYangPenuh = (sudahPenuh[0] || sudahPenuh[1] || sudahPenuh[2]);
  kirimMqtt("led_system", adaYangPenuh ? "MERAH (Penuh)" : "HIJAU (Normal)");
  kirimMqtt("matrix_text", matrixBuf);
  kirimMqtt("dfplayer", statusDfplayer.c_str());
}

// Callback perintah dari dashboard / MQTT
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  char pesan[64];
  unsigned int len = (length < sizeof(pesan) - 1) ? length : (sizeof(pesan) - 1);
  memcpy(pesan, payload, len);
  pesan[len] = '\0';

  Serial.printf("[MQTT RX] %s -> %s\n", topic, pesan);

  if (strstr(topic, "cmd/servo1")) {
    if (strcmp(pesan, "BUKA") == 0) gerakServo(0, POSISI_BUKA);
    else gerakServo(0, POSISI_TUTUP);
    publishTelemetry();
  } else if (strstr(topic, "cmd/servo2")) {
    if (strcmp(pesan, "BUKA") == 0) gerakServo(1, POSISI_BUKA);
    else gerakServo(1, POSISI_TUTUP);
    publishTelemetry();
  } else if (strstr(topic, "cmd/servo3")) {
    if (strcmp(pesan, "BUKA") == 0) gerakServo(2, POSISI_BUKA);
    else gerakServo(2, POSISI_TUTUP);
    publishTelemetry();
  }
}

void hubungkanMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  char clientId[40];
  snprintf(clientId, sizeof(clientId), "ESP32-SmartBin-%s-%04X", DEVICE_ID, (uint16_t)random(0xFFFF));

  char willTopic[96];
  snprintf(willTopic, sizeof(willTopic), "smartbin/%s/status", DEVICE_ID);

  Serial.print("[MQTT] Menghubungkan ke broker: ");
  Serial.print(MQTT_BROKER);
  Serial.print("... ");

  // Last Will: jika putus tiba-tiba, broker otomatis publish offline
  if (mqttClient.connect(clientId, MQTT_USER, MQTT_PASS, willTopic, 1, true, "offline")) {
    Serial.println("[TERHUBUNG]");
    kirimMqtt("status", "online");

    // Subscribe command topic
    char subCmd[96];
    snprintf(subCmd, sizeof(subCmd), "smartbin/%s/cmd/#", DEVICE_ID);
    mqttClient.subscribe(subCmd);
    mqttClient.subscribe("smartbin/cmd/#");

    // Langsung kirim telemetry pertama kali
    publishTelemetry();
  } else {
    Serial.printf("[GAGAL] rc=%d\n", mqttClient.state());
  }
}

void updateJaringan() {
  unsigned long now = millis();

  // 1. Kelola Wi-Fi (Non-blocking, tidak menunda fungsi offline)
  if (WiFi.status() != WL_CONNECTED) {
    if (now - tCobaWiFi >= 10000) { // Coba hubungkan tiap 10 detik
      tCobaWiFi = now;
      Serial.println("[WIFI] Menghubungkan ke Wi-Fi...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }

  // 2. Kelola MQTT
  if (!mqttClient.connected()) {
    if (now - tCobaMQTT >= 5000) { // Coba hubungkan tiap 5 detik
      tCobaMQTT = now;
      hubungkanMQTT();
    }
  } else {
    mqttClient.loop();

    // 3. Kirim Telemetry Berkala
    if (now - tKirimTelemetry >= INTERVAL_TELEMETRY) {
      tKirimTelemetry = now;
      publishTelemetry();
    }
  }
}

// ================= EVALUASI SENSOR =================
bool adaOrang() {
  for (int k = 0; k < JUMLAH_LUAR; k++)
    if (cntOrang[k] >= 1) return true;
  return false;
}

bool orangTerkonfirmasi() {
  for (int k = 0; k < JUMLAH_LUAR; k++)
    if (cntOrang[k] >= 2) return true;
  return false;
}

void evaluasiLuar(int k) {
  int d = jarakLuar[k];
  if (d > 1 && d <= BATAS_BUKA) cntOrang[k]++;
  else cntOrang[k] = 0;
  if (cntOrang[k] >= 1) waktuTerakhirOrang = millis();
}

void evaluasiDalam(int i) {
  // hanya ukur isi saat semua tutup tertutup & sudah tenang
  if (terbuka || millis() - waktuTutup < JEDA_SETELAH_TUTUP) return;
  int d = jarakDalam[i];
  if (d < 2) return; // pembacaan tidak valid

  if (d <= BATAS_PENUH) { cntPenuh[i]++; cntKosong[i] = 0; }
  else if (d >= BATAS_KOSONG) { cntKosong[i]++; cntPenuh[i] = 0; }
  else { cntPenuh[i] = 0; cntKosong[i] = 0; }

  if (!sudahPenuh[i] && cntPenuh[i] >= 4) {
    sudahPenuh[i] = true;
    idleDirty = true;
    Serial.printf("[ALERT] TONG %s PENUH\n", NAMA_TONG[i]);
    pesanSementara("MAAF TONG PENUH", 8000);
    putarSuara(2);
    waktuSuaraPenuh = millis();
    publishTelemetry(); // Langsung kirim telemetry saat status berubah
  } else if (sudahPenuh[i] && cntKosong[i] >= 4) {
    sudahPenuh[i] = false;
    idleDirty = true;
    Serial.printf("[INFO] TONG %s KEMBALI NORMAL\n", NAMA_TONG[i]);
    publishTelemetry(); // Langsung kirim telemetry saat kembali normal
  }
}

void bacaSensorBergiliran() {
  if (millis() - tSensor < INTERVAL_SENSOR) return;
  tSensor = millis();

  if (tick % 2 == 0) { // sensor luar (lebih sering)
    jarakLuar[idxLuar] = bacaUltrasonik(PIN_TRIG_LUAR[idxLuar], PIN_ECHO_LUAR[idxLuar]);
    evaluasiLuar(idxLuar);
    idxLuar = (idxLuar + 1) % JUMLAH_LUAR;
  } else {             // sensor dalam
    if (!terbuka) { // saat tutup terbuka, sensor dalam bisa melihat tutup/tangan
      jarakDalam[idxDalam] = bacaUltrasonik(PIN_TRIG_DALAM[idxDalam], PIN_ECHO_DALAM[idxDalam]);
      evaluasiDalam(idxDalam);
    }
    idxDalam = (idxDalam + 1) % JUMLAH_TONG;
  }
  tick++;
}

// ================= LOGIKA TUTUP =================
void bukaTutup() {
  bool adaYangBuka = false;
  for (int i = 0; i < JUMLAH_TONG; i++) {
    if (!sudahPenuh[i]) {
      gerakServo(i, POSISI_BUKA);
      adaYangBuka = true;
    }
  }
  if (!adaYangBuka) return;
  terbuka = true;
  waktuBuka = millis();
  waktuTerakhirOrang = millis();
  Serial.println("[ACT] TUTUP TERBUKA");
  if (SUARA_SELAMAT_DATANG) putarSuara(3);
  publishTelemetry(); // Kirim status servo TERBUKA ke dashboard seketika
}

void tutupSemua() {
  for (int i = 0; i < JUMLAH_TONG; i++) gerakServo(i, POSISI_TUTUP);
  terbuka = false;
  waktuTutup = millis();
  for (int i = 0; i < JUMLAH_TONG; i++) { cntPenuh[i] = 0; cntKosong[i] = 0; }
  Serial.println("[ACT] TUTUP MENUTUP");
  pesanSementara("TERIMA KASIH", 8000);
  putarSuara(1);
  publishTelemetry(); // Kirim status servo TERTUTUP ke dashboard seketika
}

void prosesTutup() {
  unsigned long now = millis();

  if (!terbuka) {
    if (now - waktuTutup > JEDA_SETELAH_TUTUP && orangTerkonfirmasi()) {
      bool semuaPenuh = true;
      for (int i = 0; i < JUMLAH_TONG; i++) if (!sudahPenuh[i]) semuaPenuh = false;

      if (semuaPenuh) {
        if (now - waktuSuaraPenuh > COOLDOWN_PENUH) {
          waktuSuaraPenuh = now;
          pesanSementara("MAAF SEMUA TONG PENUH", 9000);
          putarSuara(2);
        }
      } else {
        bukaTutup();
      }
    }
    return;
  }

  // sedang terbuka
  bool tanganPergi = (now - waktuTerakhirOrang > TUNDA_TUTUP);
  if ((now - waktuBuka > MIN_BUKA && tanganPergi) || (now - waktuBuka > MAX_BUKA)) {
    tutupSemua();
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[INIT] Smart Bin ESP32 - Multi-Tong + MQTT Dashboard");

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);

  for (int k = 0; k < JUMLAH_LUAR; k++) {
    pinMode(PIN_TRIG_LUAR[k], OUTPUT);
    pinMode(PIN_ECHO_LUAR[k], INPUT);
  }
  for (int i = 0; i < JUMLAH_TONG; i++) {
    pinMode(PIN_TRIG_DALAM[i], OUTPUT);
    pinMode(PIN_ECHO_DALAM[i], INPUT);
    myservo[i].setPeriodHertz(50);
    gerakServo(i, POSISI_TUTUP);
  }

  // Muat kalibrasi tersimpan dari Flash sebelum inisialisasi matrix
  muatPengaturanMatrix();
  inisialisasiMatrix();
  inisialisasiDFPlayer();

  waktuTutup = millis();
  idleDirty = true;
  Serial.println("[OK] Perangkat keras siap");
  Serial.println("[MATRIX] Kalibrasi: k=garis/teks 1,2,3=balik panel x,y=tukar panel r=balik baris h=tipe HW ?=status");
  cetakStatusMatrix();

  // Inisialisasi Jaringan Wi-Fi & MQTT (Non-blocking)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  tCobaWiFi = millis();
  Serial.println("[WIFI] Memulai koneksi background ke Wi-Fi & MQTT...");
}

// ================= LOOP =================
void loop() {
  handleSerial();
  updateMatrix();
  bacaSensorBergiliran();
  prosesTutup();
  updateServo();
  updateDFPlayer();
  updateJaringan(); // Kelola koneksi Wi-Fi & kirim telemetry MQTT tanpa membebani loop
}
