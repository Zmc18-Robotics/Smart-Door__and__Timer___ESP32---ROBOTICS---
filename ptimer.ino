#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// Definisi pin untuk I2C
#define SDA_PIN 21
#define SCL_PIN 22

// Definisi pin untuk buzzer
#define BUZZER_PIN 15

// Definisi pin untuk Relay
#define RELAY_PIN 17

// Inisialisasi LCD (alamat I2C 0x27, 16 kolom, 2 baris)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Definisi ukuran keypad
const byte ROWS = 4;
const byte COLS = 4;

// Definisi layout keypad
char keys[ROWS][COLS] = {{'1', '2', '3', 'A'},
                         {'4', '5', '6', 'B'},
                         {'7', '8', '9', 'C'},
                         {'*', '0', '#', 'D'}};

// Definisi pin untuk keypad
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

// Inisialisasi keypad
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Array interval beep dari Esp32.c (semakin kecil sisa detik, beep semakin cepat)
int beepIntervalMs[] = {900, 850, 800, 720, 640, 560, 480, 400,
                        330, 270, 220, 180, 150, 120, 100};

// State Machine
enum SystemState {
  STATE_MENU,
  STATE_TIMER_INPUT,
  STATE_SMARTDOOR_SET,
  STATE_SMARTDOOR_LOCKED,
  STATE_SMARTDOOR_UNLOCKED
};
SystemState currentState = STATE_MENU;

// Variabel Input
String inputValue = "";       // Untuk input Timer
String smartdoorPassword = ""; // Menyimpan password Smartdoor
String inputBuffer = "";      // Penampung ketikan password/PIN

// Deklarasi fungsi-fungsi
void handleKeypadInput();
void runTimer(int totalSec);
bool checkCancelDelay(int ms);
void cancelTimer();
void lcdPrint(const char *b1, const char *b2);
void beep(int duration, int frequency);
void showMainMenu();
void showInputScreen();
void showInputValue();
void showSmartdoorSetScreen();
void showSmartdoorLockedScreen();

void setup() {
  // Inisialisasi komunikasi serial
  Serial.begin(115200);

  // Inisialisasi I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();

  // Inisialisasi pin buzzer & relay
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Awalnya mati (pintu terbuka/standby)

  // Tampilkan pesan awal
  lcdPrint("  Timer ESP32   ", " Siap digunakan ");
  delay(1500);
  
  showMainMenu();
}

void loop() {
  // Selalu cek input keypad secara terus-menerus
  handleKeypadInput();
}

void handleKeypadInput() {
  static unsigned long lastKeyPress = 0; // Debouncing manual
  char key = keypad.getKey();

  if (key) {
    // Abaikan jika terdeteksi penekanan ganda dalam waktu kurang dari 200ms
    if (millis() - lastKeyPress < 200) {
      return;
    }
    lastKeyPress = millis();

    Serial.println(key);
    
    // Bunyi klik tombol standar untuk setiap penekanan tombol (premium feel)
    beep(30, 3000);

    // ==========================================
    // RATING INPUT BERDASARKAN STATE
    // ==========================================
    
    // 1. STATE: MAIN MENU
    if (currentState == STATE_MENU) {
      if (key == 'A') {
        currentState = STATE_TIMER_INPUT;
        inputValue = "";
        showInputScreen();
      } else if (key == 'B') {
        currentState = STATE_SMARTDOOR_SET;
        inputBuffer = "";
        showSmartdoorSetScreen();
      }
    }
    
    // 2. STATE: TIMER INPUT
    else if (currentState == STATE_TIMER_INPUT) {
      if (key == 'D') {
        if (inputValue.length() > 0) {
          int totalSeconds = inputValue.toInt();
          if (totalSeconds > 0) {
            inputValue = ""; 
            runTimer(totalSeconds); // Mulai hitung mundur (blocking-responsive)
          } else {
            inputValue = "";
            showInputScreen();
          }
        }
      } else if (key == 'C') {
        // Kembali ke Main Menu
        currentState = STATE_MENU;
        showMainMenu();
      } else if (key == 'A') {
        inputValue = "60";
        showInputValue();
      } else if (key == 'B') {
        inputValue = "300";
        showInputValue();
      } else if (key != '*' && key != '#') {
        if (inputValue.length() < 4) {
          inputValue += key;
          showInputValue();
        }
      }
    }
    
    // 3. STATE: SMARTDOOR SET PASSWORD
    else if (currentState == STATE_SMARTDOOR_SET) {
      if (key == 'D') {
        if (inputBuffer.length() > 0) {
          smartdoorPassword = inputBuffer;
          inputBuffer = "";
          
          // Kunci pintu: Nyalakan Relay & Bunyi Penguncian
          digitalWrite(RELAY_PIN, HIGH);
          
          lcdPrint(" Password Aktif ", " Pintu Terkunci ");
          
          // Nada Kunci: 2 beep cepat
          beep(80, 2000);
          delay(80);
          beep(80, 2000);
          delay(1500);
          
          currentState = STATE_SMARTDOOR_LOCKED;
          showSmartdoorLockedScreen();
        }
      } else if (key == 'C') {
        // Kembali ke Main Menu
        inputBuffer = "";
        currentState = STATE_MENU;
        showMainMenu();
      } else if (key != '*' && key != '#' && key != 'A' && key != 'B') {
        if (inputBuffer.length() < 8) {
          inputBuffer += key;
          showSmartdoorSetScreen();
        }
      }
    }
    
    // 4. STATE: SMARTDOOR LOCKED (LOGIN PIN)
    else if (currentState == STATE_SMARTDOOR_LOCKED) {
      if (key == 'D') {
        if (inputBuffer.length() > 0) {
          if (inputBuffer == smartdoorPassword) {
            // PIN Benar: Matikan Relay (Pintu Terbuka)
            digitalWrite(RELAY_PIN, LOW);
            lcdPrint(" Akses Diterima  ", " Pintu Terbuka   ");
            
            // Nada Sukses: Melodi menaik
            beep(80, 2000);
            delay(40);
            beep(80, 2500);
            delay(40);
            beep(150, 3000);
            
            inputBuffer = "";
            currentState = STATE_SMARTDOOR_UNLOCKED;
          } else {
            // PIN Salah
            lcdPrint(" Akses Ditolak   ", "   PIN Salah!   ");
            
            // Nada Gagal: Nada rendah panjang
            beep(350, 800);
            delay(80);
            beep(350, 800);
            
            delay(1000);
            inputBuffer = "";
            showSmartdoorLockedScreen();
          }
        }
      } else if (key == 'C') {
        if (inputBuffer.length() > 0) {
          // Clear ketikan jika sedang mengetik
          inputBuffer = "";
          showSmartdoorLockedScreen();
        } else {
          // Kembali ke Main Menu & Matikan relay demi keamanan
          digitalWrite(RELAY_PIN, LOW);
          currentState = STATE_MENU;
          showMainMenu();
        }
      } else if (key != '*' && key != '#' && key != 'A' && key != 'B') {
        if (inputBuffer.length() < 8) {
          inputBuffer += key;
          showSmartdoorLockedScreen();
        }
      }
    }
    
    // 5. STATE: SMARTDOOR UNLOCKED
    else if (currentState == STATE_SMARTDOOR_UNLOCKED) {
      if (key == 'C') {
        // Kunci Kembali (Reset): Nyalakan Relay kembali & Harus Login
        digitalWrite(RELAY_PIN, HIGH);
        
        lcdPrint(" Mengunci Pintu ", "  Harap Tunggu  ");
        
        // Nada Kunci
        beep(80, 2000);
        delay(80);
        beep(80, 2000);
        delay(1500);
        
        currentState = STATE_SMARTDOOR_LOCKED;
        inputBuffer = "";
        showSmartdoorLockedScreen();
      }
    }
  }
}

void showMainMenu() {
  lcdPrint("A: Timer        ", "B: Smartdoor    ");
}

void showInputScreen() {
  lcdPrint("Set timer (dtk):", "> ");
}

void showInputValue() {
  String line2 = "> " + inputValue;
  lcdPrint("Set timer (dtk):", line2.c_str());
}

void showSmartdoorSetScreen() {
  String line2 = "> " + inputBuffer;
  lcdPrint("Atur Password:", line2.c_str());
}

void showSmartdoorLockedScreen() {
  // Samarkan ketikan password dengan karakter '*'
  String mask = "";
  for (size_t i = 0; i < inputBuffer.length(); i++) {
    mask += "*";
  }
  String line2 = "> " + mask;
  lcdPrint("Masukkan PIN:", line2.c_str());
}

void runTimer(int totalSec) {
  char buf1[17], buf2[17];
  int mm = totalSec / 60;
  int ss = totalSec % 60;
  
  snprintf(buf1, sizeof(buf1), "  Timer: %02d:%02d  ", mm, ss);
  lcdPrint(buf1, "  Mulai hitung  ");
  
  // Delay 1.5 detik sambil memantau tombol Cancel 'C'
  if (checkCancelDelay(1500)) {
    cancelTimer();
    return;
  }

  for (int detik = totalSec; detik >= 1; detik--) {
    int m = detik / 60;
    int s = detik % 60;
    snprintf(buf1, sizeof(buf1), "  Hitung Mundur ");
    snprintf(buf2, sizeof(buf2), "     %02d:%02d      ", m, s);
    lcdPrint(buf1, buf2);

    // Delay 100ms pertama (sama seperti struktur delay Esp32.c)
    if (checkCancelDelay(100)) {
      cancelTimer();
      return;
    }

    // Beep: makin cepat di 15 detik terakhir
    if (detik <= 15) {
      int idx = 15 - detik;
      int interval = beepIntervalMs[idx];
      int beepLama = 50;
      
      // Frekuensi dinamis (dikalikan 2 agar nyaring)
      int freq = (880 + (idx * 80)) * 2;
      if (freq > 4000) freq = 4000;

      unsigned long mulai = millis();
      while (millis() - mulai < 900) {
        unsigned long sisa = 900 - (millis() - mulai);
        if (sisa < (unsigned long)beepLama) {
          break;
        }

        beep(beepLama, freq);

        unsigned long jedaAktual = interval - beepLama;
        unsigned long sisaSetelah = 900 - (millis() - mulai);
        if (jedaAktual > sisaSetelah) {
          if (checkCancelDelay(sisaSetelah)) {
            cancelTimer();
            return;
          }
          break;
        }
        
        if (checkCancelDelay(jedaAktual)) {
          cancelTimer();
          return;
        }
      }
    } else {
      // Delay sisa 900ms untuk melengkapi 1 detik
      if (checkCancelDelay(900)) {
        cancelTimer();
        return;
      }
    }
  }

  // Jika sukses selesai tanpa dibatalkan
  lcdPrint("  Hitung Mundur ", "     00:00      ");
  
  // Pembuat kedipan LCD backlight sebagai pemanis visual
  for (int b = 0; b < 3; b++) {
    lcd.noBacklight();
    if (checkCancelDelay(150)) { cancelTimer(); return; }
    lcd.backlight();
    if (checkCancelDelay(150)) { cancelTimer(); return; }
  }

  if (checkCancelDelay(300)) {
    cancelTimer();
    return;
  }
  lcdPrint(" ---Time out--- ", "");

  // Melodi selesai (C5, E5, G5, C6, MI2, C6, MI2) dari Esp32.c
  int nada[] = {523, 659, 784, 1047, 1319, 1047, 1319};
  int lama[] = {120, 120, 120, 200, 400, 150, 700};
  for (int i = 0; i < 7; i++) {
    beep(lama[i], nada[i]);
    if (checkCancelDelay(40 + 20)) {
      cancelTimer();
      return;
    }
  }

  // Nyalakan Relay di Pin 17 setelah melodi selesai
  digitalWrite(RELAY_PIN, HIGH);
  lcdPrint(" ---Time out--- ", " Relay: MENYALA ");

  // Menyala selama 3 detik penuh
  if (checkCancelDelay(3000)) {
    digitalWrite(RELAY_PIN, LOW);
    cancelTimer();
    return;
  }
  
  // Matikan Relay setelah selesai menyala 3 detik
  digitalWrite(RELAY_PIN, LOW);

  // Kembali ke Main Menu
  currentState = STATE_MENU;
  showMainMenu();
}

// Fungsi pembantu untuk memantau tombol Cancel 'C' saat terjadi delay
bool checkCancelDelay(int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    char key = keypad.getKey();
    if (key == 'C') {
      return true; // Dibatalkan!
    }
    delay(10);
  }
  return false;
}

// Menampilkan pesan batal
void cancelTimer() {
  digitalWrite(RELAY_PIN, LOW); // Pastikan relay mati
  lcdPrint("  Timer Batal!  ", "  Kembali...    ");
  delay(1500);
  currentState = STATE_MENU;
  showMainMenu();
}

// Helper untuk cetak LCD ber-padding 16 karakter (mencegah flickering)
void lcdPrint(const char *b1, const char *b2) {
  char buf[17];
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "%-16s", b1);
  lcd.print(buf);
  lcd.setCursor(0, 1);
  snprintf(buf, sizeof(buf), "%-16s", b2);
  lcd.print(buf);
}

// Fungsi bunyi buzzer pasif
void beep(int duration, int frequency) {
  unsigned long startMillis = millis();
  unsigned int halfPeriod = 1000000 / frequency / 2;
  
  while (millis() - startMillis < duration) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(halfPeriod); 
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(halfPeriod);
  }
}