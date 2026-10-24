#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <math.h>

// ==========================================
// CONFIGURAÇÕES DE REDE E THINGSPEAK
// ==========================================
const char* ssid = "Maurício 2.4";
const char* password = "9614m";
String writeAPIKey = "ULAHGB"; 

// ==========================================
// MAPEAMENTO DE PINOS
// ==========================================
#define DHTPIN 4
#define DHTTYPE DHT22

// Pinos do Amplificador de Som I2S (MAX98357A)
#define I2S_LRC 25
#define I2S_BCLK 26
#define I2S_DOUT 27

// Configuração do Display OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================
// OBJETOS E VARIÁVEIS DO SISTEMA
// ==========================================
DHT dht(DHTPIN, DHTTYPE);

// Variáveis dos Sensores e Física
float temperatura = 0.0;
float umidade = 0.0;
float indiceCalor = 0.0;
float pontoOrvalho = 0.0;
float umidadeAbsoluta = 0.0;
float pressaoVapor = 0.0;

// Controle de Telas e Temporizadores
unsigned long t_leitura = 0;
unsigned long t_envio = 0;
unsigned long t_display = 0;
int paginaDisplay = 1;

// Configuração de Áudio I2S (16kHz)
const int SAMPLE_RATE = 16000;

// Protótipos de Funções
void setupI2S();
void playTone(int freq, int duration_ms);
void playStartupMelody();
void playAlarm();
void playTemperatureTone(float temp);
void animacaoAberturaApple();
void calcularFisicaDHT(float t, float h);
void atualizarDisplay();
void enviarThingSpeak();

void setup() {
  Serial.begin(115200);

  // Inicializa o I2S para o amplificador MAX98357A
  setupI2S();

  // 1. Inicializa o Display OLED primeiro para poder rodar a animação
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("Falha ao iniciar o OLED"));
    for(;;);
  }

  // 2. Executa a Animação de Abertura Estilo Apple + Música
  animacaoAberturaApple();

  // 3. Inicializa o Sensor DHT22
  dht.begin();

  // 4. Conexão WiFi
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 20);
  display.println("Conectando WiFi...");
  display.setCursor(0, 35);
  display.println(ssid);
  display.display();
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("WiFi Conectado!");
  display.display();
  delay(1500); 
}

void loop() {
  unsigned long tempoAtual = millis();

  // ==========================================
  // 1. LEITURA E CÁLCULOS (A cada 2 segundos)
  // ==========================================
  if (tempoAtual - t_leitura >= 2000) { 
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (!isnan(t) && !isnan(h)) {
      temperatura = t;
      umidade = h;
      indiceCalor = dht.computeHeatIndex(temperatura, umidade, false);
      
      calcularFisicaDHT(temperatura, umidade);

      if (temperatura >= 32.0 || indiceCalor >= 32.0) {
        playAlarm();
      } else {
        playTemperatureTone(temperatura);
      }

      atualizarDisplay();
    } else {
      Serial.println("Falha na leitura do DHT22!");
    }
    t_leitura = tempoAtual; 
  }

  // ==========================================
  // 2. ALTERNÂNCIA DE PÁGINAS NO DISPLAY (A cada 4s)
  // ==========================================
  if (tempoAtual - t_display >= 4000) {
    paginaDisplay = (paginaDisplay == 1) ? 2 : 1;
    atualizarDisplay();
    t_display = tempoAtual;
  }

  // ==========================================
  // 3. ENVIO PARA O THINGSPEAK (A cada 20 segundos)
  // ==========================================
  if (WiFi.status() == WL_CONNECTED && (tempoAtual - t_envio >= 20000)) {
    enviarThingSpeak();
    t_envio = tempoAtual;
  }
}

// ==========================================
// FUNÇÕES DE ÁUDIO I2S (MAX98357A)
// ==========================================

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void playTone(int freq, int duration_ms) {
  if (freq <= 0) {
    delay(duration_ms);
    return;
  }
  int num_samples = (SAMPLE_RATE * duration_ms) / 1000;
  int16_t *buffer = (int16_t *)malloc(512 * sizeof(int16_t));
  if (!buffer) return;
  
  int samples_sent = 0;
  while (samples_sent < num_samples) {
    int chunk = min(256, num_samples - samples_sent);
    for (int i = 0; i < chunk; i++) {
      float t = (float)(samples_sent + i) / SAMPLE_RATE;
      int16_t sample = (sin(2 * 3.14159265 * freq * t) > 0) ? 12000 : -12000;
      buffer[2 * i] = sample;     
      buffer[2 * i + 1] = sample; 
    }
    size_t bytes_written;
    i2s_write(I2S_NUM_0, buffer, chunk * 4, &bytes_written, portMAX_DELAY);
    samples_sent += chunk;
  }
  free(buffer);
}

void playStartupMelody() {
  int notes[] = {523, 659, 784, 1046}; // C5, E5, G5, C6
  int durations[] = {120, 120, 120, 250};
  for (int i = 0; i < 4; i++) {
    playTone(notes[i], durations[i]);
    delay(40);
  }
}

void playAlarm() {
  for (int i = 0; i < 3; i++) {
    playTone(1200, 150);
    playTone(800, 150);
  }
}

void playTemperatureTone(float temp) {
  int freq = map((int)temp, 15, 40, 300, 1200);
  freq = constrain(freq, 200, 2000);
  playTone(freq, 300);
}

// ==========================================
// ANIMAÇÃO DE ABERTURA ESTILO APPLE (PI VI)
// ==========================================
void animacaoAberturaApple() {
  // Passo 1: Moldura expandindo suavemente do centro
  for (int w = 10; w <= 124; w += 8) {
    display.clearDisplay();
    int h = 32;
    int x = (128 - w) / 2;
    int y = (64 - h) / 2;
    display.drawRect(x, y, w, h, WHITE);
    display.display();
    delay(15);
  }

  // Toca a primeira parte da música junto com o efeito visual
  playTone(523, 120); // Nota C5

  // Passo 2: Moldura preenche o display com estilo minimalista
  display.clearDisplay();
  display.drawRect(2, 2, 124, 60, WHITE);
  display.drawRect(4, 4, 120, 56, WHITE);
  display.display();
  playTone(659, 120); // Nota E5
  delay(100);

  // Passo 3: Exibe o texto principal "PI VI" com fonte grande e centralizada
  display.clearDisplay();
  display.setTextSize(3); // Fonte grande
  display.setTextColor(WHITE);
  display.setCursor(26, 16);
  display.println(F("PI VI"));
  display.display();
  playTone(784, 120); // Nota G5
  delay(300);

  // Passo 4: Adiciona o subtítulo tecnológico estilo Apple
  display.clearDisplay();
  display.setTextSize(3);
  display.setCursor(26, 10);
  display.println(F("PI VI"));
  
  display.setTextSize(1);
  display.setCursor(24, 48);
  display.println(F("Smart IoT System"));
  display.display();
  
  playTone(1046, 300); // Nota final C6 (clímax da abertura)
  delay(1200); // Segura a tela de apresentação antes de iniciar o loop principal
}

// ==========================================
// CÁLCULOS FÍSICOS E MATEMÁTICOS DO DHT22
// ==========================================
void calcularFisicaDHT(float t, float h) {
  pressaoVapor = 6.112 * exp((17.67 * t) / (t + 243.5)) * (h / 100.0);
  float gamma = log(h / 100.0) + (17.62 * t) / (243.12 + t);
  pontoOrvalho = (243.12 * gamma) / (17.62 - gamma);
  umidadeAbsoluta = (216.7 * pressaoVapor) / (t + 273.15);
}

// ==========================================
// FUNÇÕES DE EXIBIÇÃO NO OLED
// ==========================================
void atualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  if (paginaDisplay == 1) {
    display.setCursor(15, 0); 
    display.println("IoT - Monitoracao");
    display.drawLine(0, 10, 128, 10, WHITE);
    
    display.setCursor(0, 18); 
    display.print("Temperatura: "); 
    display.print(temperatura, 1); 
    display.println(" C");
    
    display.setCursor(0, 32); 
    display.print("Umidade:     "); 
    display.print((int)umidade); 
    display.println(" %");
    
    display.setCursor(0, 46); 
    display.print("Sensacao:    "); 
    display.print(indiceCalor, 1); 
    display.println(" C");
  } else {
    display.setCursor(10, 0); 
    display.println("Fisica & Mat DHT22");
    display.drawLine(0, 10, 128, 10, WHITE);
    
    display.setCursor(0, 18); 
    display.print("Pt.Orvalho:  "); 
    display.print(pontoOrvalho, 1); 
    display.println(" C");
    
    display.setCursor(0, 32); 
    display.print("Umid.Abs:    "); 
    display.print(umidadeAbsoluta, 1); 
    display.println(" g/m3");
    
    display.setCursor(0, 46); 
    display.print("Press.Vapor: "); 
    display.print(pressaoVapor, 1); 
    display.println(" hPa");
  }

  display.display();
}

// ==========================================
// ENVIO PARA O THINGSPEAK
// ==========================================
void enviarThingSpeak() {
  HTTPClient http;
  
  String url = "http://api.thingspeak.com/update?api_key=" + writeAPIKey + 
               "&field1=" + String(temperatura) + 
               "&field2=" + String(umidade) + 
               "&field3=" + String(indiceCalor) +
               "&field4=" + String(pontoOrvalho) +
               "&field5=" + String(umidadeAbsoluta);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    Serial.println("Dados enviados ao ThingSpeak com sucesso! HTTP Code: " + String(httpCode));
  } else {
    Serial.println("Erro ao enviar para ThingSpeak.");
  }
  
  http.end();
}
