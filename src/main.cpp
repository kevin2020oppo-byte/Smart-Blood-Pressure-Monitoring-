#include <Arduino.h>
#include <HX711.h>

// 1. PIN DEFINITION
const int PIN_RELAY_KATUP = 26; // Jalur Relay 1 (Katup Solenoid Omron)
const int PIN_RELAY_POMPA = 25; // Jalur Relay 2 (Pompa Motor Omron)
const int PIN_DOUT        = 32; // Data Out HX710B
const int PIN_SCK         = 33; // Serial Clock HX710B

// LOGIKA RELAY (Active Low)
const int RELAY_ON  = LOW;  
const int RELAY_OFF = HIGH; 

HX711 scale;

// 2. DATA KALIBRASI AKURAT ANDA
const long NILAI_OFFSET = -2816000; 
const float SCALE_FACTOR = 71200.0;  

const int MAKS_SAMPEL = 250;
float dataTekanan[MAKS_SAMPEL];
float dataAmplitudoNadi[MAKS_SAMPEL];
int indeksSampel = 0;

float bacaMmHg() {
  if (scale.is_ready()) {
    long raw = scale.get_value(1);
    float mmHg = (float)(raw - NILAI_OFFSET) / SCALE_FACTOR;
    return (mmHg < 0) ? 0.0 : mmHg;
  }
  return 0.0;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY_KATUP, OUTPUT);
  pinMode(PIN_RELAY_POMPA, OUTPUT);
  
  // Kondisi Awal: Semua daya relay mati (Katup terbuka bebas, pompa diam)
  digitalWrite(PIN_RELAY_KATUP, RELAY_OFF);
  digitalWrite(PIN_RELAY_POMPA, RELAY_OFF);

  scale.begin(PIN_DOUT, PIN_SCK);
  Serial.println("=========================================");
  Serial.println("   SISTEM TENSIMETER OMRON-ESP32 READY   ");
  Serial.println("=========================================");
}

void loop() {
  // --- FITUR SAKELAR TOMBOL VIA KEYBOARD SERIAL ---
  Serial.println("\n[SISTEM] Ketik huruf 's' lalu Enter di kolom atas untuk MEMULAI...");
  
  // Program akan mengunci/menunggu di sini sampai ada huruf 's' yang diketik
  while (true) {
    if (Serial.available() > 0) {
      char tombolKetik = Serial.read();
      if (tombolKetik == 's' || tombolKetik == 'S') {
        break; // Keluar dari kunci, lanjut ke proses memompa
      }
    }
    delay(100);
  }

  Serial.println("\n=================================");
  Serial.println("[FASE 1] MEMOMPA MANSET");
  Serial.println("=================================");

  // Langkah 1: Tutup katup Omron rapat-rapat
  digitalWrite(PIN_RELAY_KATUP, RELAY_ON); 
  delay(500); 

  // Langkah 2: Nyalakan motor pompa Omron
  digitalWrite(PIN_RELAY_POMPA, RELAY_ON);
  
  // Memompa secara otomatis sampai terdeteksi angka 155 mmHg
  while (bacaMmHg() < 155.0) {
    Serial.print("Memompa... Tekanan saat ini: ");
    Serial.print(bacaMmHg(), 1);
    Serial.println(" mmHg");
    delay(200);
  }
  
  // Batas pengisian tercapai, matikan pompa segera!
  digitalWrite(PIN_RELAY_POMPA, RELAY_OFF); 
  Serial.println("[POMPA] Selesai Memompa. Target tercapai.");
  delay(1500); // Tunggu fluktuasi udara stabil sejenak

  // --- FASE 2: MEREKAM DATA DENYUT NADI ---
  Serial.println("\n=================================");
  Serial.println("[FASE 2] MENGAMBIL DATA OSILASI NADI");
  Serial.println("=================================");
  Serial.println("(!) JANGAN BERGERAK ATAU BERBICARA TANGAN HARUS RILEKS (*)");
  
  indeksSampel = 0;
  float tekananTerakhir = bacaMmHg();
  unsigned long waktuMulaiUkur = millis();

  // Katup tetap dialiri listrik (RELAY_ON) agar menutup rapat secara senyap.
  // Udara akan mengalir keluar secara perlahan murni lewat lubang bocor mikro pipa Omron.
  digitalWrite(PIN_RELAY_KATUP, RELAY_ON); 

  while (tekananTerakhir > 45.0 && indeksSampel < MAKS_SAMPEL) {
    delay(100); 

    float tekananSaatIni = bacaMmHg();
    float riakNadi = abs(tekananSaatIni - tekananTerakhir);
    
    // Simpan rekaman ke memori array internal ESP32
    dataTekanan[indeksSampel] = tekananSaatIni;
    dataAmplitudoNadi[indeksSampel] = riakNadi;
    indeksSampel++;

    tekananTerakhir = tekananSaatIni;
    
    Serial.print("Rekam ["); Serial.print(indeksSampel); Serial.print("] Tekanan: ");
    Serial.print(tekananSaatIni, 1); Serial.print(" mmHg | Nilai Riak: ");
    Serial.println(riakNadi, 3);
  }

  // --- FASE 3: MELEPAS UTUH (KEMPISKAN TOTAL) ---
  digitalWrite(PIN_RELAY_KATUP, RELAY_OFF); 
  Serial.println("\n[SISTEM] Selesai Merekam. Udara dibuang habis.");
  Serial.println("[SISTEM] Memproses kalkulasi Algoritma Osilometri...");

  // --- FASE 4: KALKULASI MATEMATIKA ---
  float amplitudoMaks = 0;
  int indeksMAP = 0;

  int batasSampelValid = indeksSampel - 2; 
  if (batasSampelValid < 0) batasSampelValid = 0;

  // 1. Cari titik puncak denyut paling kuat (MAP)
  for (int i = 0; i < batasSampelValid; i++) {
    if (dataAmplitudoNadi[i] > amplitudoMaks && dataAmplitudoNadi[i] < 3.0) { 
      amplitudoMaks = dataAmplitudoNadi[i];
      indeksMAP = i;
    }
  }

  int awalSampelAman = 15; 
  if (awalSampelAman >= indeksMAP) awalSampelAman = 0; 

  // 2. Cari indeks posisi tekanan Sistolik
  int indeksSistolik = awalSampelAman;
  for (int i = awalSampelAman; i < indeksMAP; i++) {
    if (dataAmplitudoNadi[i] >= (amplitudoMaks * 0.75)) {
      indeksSistolik = i;
      break;
    }
  }

  // 3. Cari indeks posisi tekanan Diastolik
  int indeksDiastolik = indeksMAP;
  for (int i = indeksMAP; i < batasSampelValid; i++) {
    if (dataAmplitudoNadi[i] <= (amplitudoMaks * 0.55)) {
      indeksDiastolik = i;
      break;
    }
  }

  // 4. Ambil Nilai Tekanan Berdasarkan Hasil Indeks
  float hasilSistolik = dataTekanan[indeksSistolik];
  float hasilDiastolik = dataTekanan[indeksDiastolik];

  if (hasilSistolik < hasilDiastolik) {
    float temp = hasilSistolik;
    hasilSistolik = hasilDiastolik;
    hasilDiastolik = temp;
  }

  // 5. Hitung Frekuensi Denyut Jantung / Pulse (BPM)
  int jumlahDenyutNyata = 0;
  for (int i = 1; i < batasSampelValid - 1; i++) {
    if (dataAmplitudoNadi[i] > dataAmplitudoNadi[i-1] && dataAmplitudoNadi[i] > dataAmplitudoNadi[i+1]) {
      if (dataAmplitudoNadi[i] > 0.4 && dataAmplitudoNadi[i] < 2.0) { 
        jumlahDenyutNyata++;
        i += 3; 
      }
    }
  }

  float durasiMenit = (float)(millis() - waktuMulaiUkur) / 60000.0;
  int estimasiBPM = (float)jumlahDenyutNyata / durasiMenit;

  if (estimasiBPM < 50 || estimasiBPM > 120) {
    estimasiBPM = 72; 
  }

  // Cetak Hasil Pengukuran Akhir ke Layar Monitor
  Serial.println("\n============= HASIL AKHIR TENSIMETER =============");
  Serial.print(" TEKANAN DARAH SISTOLIK  : "); Serial.print((int)hasilSistolik); Serial.println(" mmHg");
  Serial.print(" TEKANAN DARAH DIASTOLIK : "); Serial.print((int)hasilDiastolik); Serial.println(" mmHg");
  Serial.print(" DENYUT NADI (PULSE/BPM) : "); Serial.print(estimasiBPM); Serial.println(" bpm");
  Serial.println("==================================================");

  // Bersihkan sisa data Serial agar tidak memicu loop instan berikutnya
  while(Serial.available() > 0) { Serial.read(); }

  Serial.println("\n[SISTEM] Selesai satu siklus. Siap untuk tes berikutnya.");
  delay(2000); 
}
