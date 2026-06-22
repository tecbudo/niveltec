/**
 * @file niveltec.ino
 * @brief Monitor de nível de água com sensores VL53L0X, Firebase e economia de energia.
 */

#include <Arduino.h>
#include <Wire.h>
#include <freertos/semphr.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "Conexao.h"
#include "Sensores.h"
#include "display.h"
#include "Armazenamento.h"
#include "DeviceInfo.h"
// ==================== Definições de Pinos ====================
#define PINO_LED        15
#define PINO_BOTAO      0
#define I2C_SDA         33
#define I2C_SCL         35
#define XSHUT_SENSOR1   39
#define XSHUT_SENSOR2   37

// ==================== Estrutura de Configuração ====================
struct Config {
  int intervaloMedicao = 6;
  int amostrasMedia = 5;
  String modoEnvio = "ambos";
  String modoOperacao = "sempreAtivo";
  bool displayLigado = true;
};

// ==================== Variáveis Globais ====================
Config configAtual;
Preferences prefs;
SemaphoreHandle_t xConfigMutex;
SemaphoreHandle_t xDisplayMutex;
SemaphoreHandle_t xI2CMutex;  // ← Mutex para proteger acesso ao I2C

float ultimaLeituraSensor1 = 0;
float ultimaLeituraSensor2 = 0;
float ultimaMedia = 0;
unsigned long ultimaLeituraTimestamp = 0;
bool deviceInfoEnviado = false;

// ==================== Protótipos ====================
void tarefaComunicacao(void *arg);
void tarefaMedicao(void *arg);
void tarefaDisplay(void *arg);
void tarefaBotao(void *arg);
void tarefaLED(void *arg);
void scanI2C();
void tarefaVerificaConexao(void *arg);
// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Iniciando sistema de monitoramento...");

  // Configurar pinos
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(XSHUT_SENSOR1, OUTPUT);
  pinMode(XSHUT_SENSOR2, OUTPUT);
  // Reset dos sensores
digitalWrite(XSHUT_SENSOR1, LOW);
digitalWrite(XSHUT_SENSOR2, LOW);
delay(10);
digitalWrite(XSHUT_SENSOR1, HIGH);
digitalWrite(XSHUT_SENSOR2, HIGH);
delay(10);

  // Inicializar I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(200);
  scanI2C();
  delay(1000);
// Montar sistema de arquivos para pendentes
if (!Armazenamento::begin()) {
    Serial.println("Falha ao inicializar LittleFS");
    if (display.isDisponivel()) display.printLog("ERRO FS");
} else {
    Serial.println("LittleFS montado");
}
  // Inicializar display
  display.begin();
  if (display.isDisponivel()) {
    display.clear();
    display.print("INICIANDO...");
    display.setStatus(1);
  }

  // Carregar configuração local
  prefs.begin("niveltec", false);
  configAtual.intervaloMedicao = prefs.getInt("intervalo", 600);
  configAtual.amostrasMedia = prefs.getInt("amostras", 5);
  configAtual.modoEnvio = prefs.getString("modoEnvio", "ambos");
  configAtual.modoOperacao = prefs.getString("modoOperacao", "sempreAtivo");
  configAtual.displayLigado = prefs.getBool("displayLigado", true);
  prefs.end();

  if (configAtual.displayLigado && display.isDisponivel()) display.on();
  else if (display.isDisponivel()) display.off();

  // Criar mutexes
  xConfigMutex = xSemaphoreCreateMutex();
  xDisplayMutex = xSemaphoreCreateMutex();
  xI2CMutex = xSemaphoreCreateMutex();

  // Inicializar sensores
  sensores.iniciar(XSHUT_SENSOR1, XSHUT_SENSOR2);
  Serial.println("Sensores inicializados");
  if (display.isDisponivel()) display.printLog("Sensores OK");

  // Conectar WiFi e Firebase
  setupConexao();
  Serial.println("Conexões inicializadas");
  if (display.isDisponivel()) display.printLog("Conexao OK");
  updateDeviceStatus("ativo");
  // Inicializar DeviceInfo (armazena MAC, timestamps, bootCount)
DeviceInfo::init();
if (display.isDisponivel()) display.printLog("DeviceInfo OK");

// Tentar enviar DeviceInfo se estiver conectado
if (isConnected()) {
    if (DeviceInfo::saveToFirebase()) {
        deviceInfoEnviado = true;
        Serial.println("DeviceInfo enviado ao Firebase");
        if (display.isDisponivel()) display.printLog("Info enviada");
    }
}

// Tentar reenviar dados pendentes que possam ter ficado da execução anterior
if (isConnected()) {
    Armazenamento::enviarPendentes();
}

  // Criar tarefas FreeRTOS
  xTaskCreate(tarefaComunicacao, "Comunicacao", 8192, NULL, 2, NULL);  // ← Reative após corrigir Firebase
  xTaskCreate(tarefaMedicao, "Medicao", 8192, NULL, 1, NULL);
  xTaskCreate(tarefaDisplay, "Display", 4096, NULL, 1, NULL);
  xTaskCreate(tarefaBotao, "Botao", 2048, NULL, 1, NULL);
  xTaskCreate(tarefaLED, "LED", 2048, NULL, 1, NULL);
  xTaskCreate(tarefaVerificaConexao, "VerifConex", 8192, NULL, 1, NULL);

  Serial.println("Sistema pronto");
  if (display.isDisponivel()) display.print("PRONTO");
}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// ==================== Scan I2C (Debug) ====================
void scanI2C() {
  Serial.println("=== Escaneando I2C ===");
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Dispositivo em 0x%02X\n", addr);
      count++;
    }
    delay(10);
  }
  if (count == 0) Serial.println("Nenhum dispositivo I2C detectado!");
  Serial.println("=== Fim do scan ===");
}

// ==================== Tarefa de Comunicação (Opcional) ====================
void tarefaComunicacao(void *arg) {
  while (1) {
    esp_task_wdt_reset();
    
    if (isConnected()) {
      int intervalo, amostras;
      String modoEnvio, modoOperacao;
      bool displayLigado;
      
      if (getConfigFromFirebase(intervalo, amostras, modoEnvio, modoOperacao, displayLigado)) {
        bool mudou = false;
        
        if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
          if (intervalo != configAtual.intervaloMedicao) { configAtual.intervaloMedicao = intervalo; mudou = true; }
          if (amostras != configAtual.amostrasMedia) { configAtual.amostrasMedia = amostras; mudou = true; }
          if (modoEnvio != configAtual.modoEnvio) { configAtual.modoEnvio = modoEnvio; mudou = true; }
          if (modoOperacao != configAtual.modoOperacao) { configAtual.modoOperacao = modoOperacao; mudou = true; }
          if (displayLigado != configAtual.displayLigado) { configAtual.displayLigado = displayLigado; mudou = true; }
          xSemaphoreGive(xConfigMutex);
        }
        
        if (mudou) {
          prefs.begin("niveltec", false);
          prefs.putInt("intervalo", configAtual.intervaloMedicao);
          prefs.putInt("amostras", configAtual.amostrasMedia);
          prefs.putString("modoEnvio", configAtual.modoEnvio);
          prefs.putString("modoOperacao", configAtual.modoOperacao);
          prefs.putBool("displayLigado", configAtual.displayLigado);
          prefs.end();
          
          if (display.isDisponivel()) {
            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
              if (configAtual.displayLigado) display.on(); else display.off();
              xSemaphoreGive(xI2CMutex);
            }
          }
          Serial.println("Configuração atualizada via Firebase");
        }
      }
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// ==================== Tarefa de Medição (CORRIGIDA E LIMPA) ====================
void tarefaMedicao(void *arg) {
  Serial.println(">>> [MED] Tarefa iniciada");
  esp_task_wdt_reset();
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  
  while (1) {
    esp_task_wdt_reset();
    
    int intervalo, amostras;
    if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      intervalo = configAtual.intervaloMedicao;
      amostras = configAtual.amostrasMedia;
      xSemaphoreGive(xConfigMutex);
    } else {
      intervalo = 600; amostras = 5;  // Fallback
    }

    vTaskDelay( intervalo* 1000 / portTICK_PERIOD_MS);  // ← Mantenha 10s para testes; altere para intervalo em produção
    
    esp_task_wdt_reset();
    
    // Log no display (protegido por mutex I2C)
    if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      display.printLog("Medindo...");
      xSemaphoreGive(xI2CMutex);
    }
    
    float soma1 = 0, soma2 = 0;
    int amostrasValidas1 = 0, amostrasValidas2 = 0;
    
    for (int i = 0; i < amostras; i++) {
      esp_task_wdt_reset();
      
      float d1 = -1, d2 = -1;
      
      // Leitura dos sensores COM mutex e timeout
      if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        unsigned long start = millis();
        d1 = sensores.lerDistancia(0);
        if (millis() - start > 300) d1 = -1;
        
        start = millis();
        d2 = sensores.lerDistancia(1);
        if (millis() - start > 300) d2 = -1;
        
        xSemaphoreGive(xI2CMutex);
      }
      
      if (d1 >= 10 && d1 <= 2000) { soma1 += d1; amostrasValidas1++; }
      if (d2 >= 10 && d2 <= 2000) { soma2 += d2; amostrasValidas2++; }
      
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    float media1 = (amostrasValidas1 > 0) ? soma1 / amostrasValidas1 : -1;
    float media2 = (amostrasValidas2 > 0) ? soma2 / amostrasValidas2 : -1;
    float mediaGeral = (media1 > 0 && media2 > 0) ? (media1 + media2) / 2 : -1;
    
    ultimaLeituraSensor1 = media1;
    ultimaLeituraSensor2 = media2;
    ultimaMedia = mediaGeral;
    ultimaLeituraTimestamp = millis();
    
    // Envio para Firebase
    if (isConnected() && mediaGeral > 0) {
      String modo;
      if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        modo = configAtual.modoEnvio;
        xSemaphoreGive(xConfigMutex);
      }
      enviarDados(media1, media2, mediaGeral, modo);
      
      if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        display.printLog("Dados enviados");
        xSemaphoreGive(xI2CMutex);
      }
    } else {
    Serial.println("Falha na conexão - dados não enviados");
    if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        display.printLog("Falha envio");
        xSemaphoreGive(xI2CMutex);
    }
    // Salvar pendente
    Armazenamento::salvarPendente(media1, media2, mediaGeral, getTimestamp(), configAtual.modoEnvio);
}
    
    // Deep sleep (se configurado)
    bool hibernar;
    if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      hibernar = (configAtual.modoOperacao == "hibernar");
      xSemaphoreGive(xConfigMutex);
    } else {
      hibernar = false;
    }
    
    if (hibernar) {
      if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        display.off();
        xSemaphoreGive(xI2CMutex);
      }
      esp_sleep_enable_timer_wakeup(intervalo * 1000000LL);
      esp_deep_sleep_start();
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ==================== Tarefa de Display (COM MUTEX I2C) ====================
void tarefaDisplay(void *arg) {
  while (1) {
    esp_task_wdt_reset();
    
    bool displayLigado;
    if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      displayLigado = configAtual.displayLigado;
      xSemaphoreGive(xConfigMutex);
    } else {
      displayLigado = true;  // Fallback
    }
    
    // ← Protege TODOS os acessos ao display (I2C) com mutex
    if (displayLigado && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      display.showTime(getFormattedTime());
      display.setStatus(isConnected() ? 2 : 1);
      
      if (ultimaMedia > 0) {
        display.exibirNivel(ultimaMedia, " mm");
      } else {
        display.exibirNivel(0, " mm");
      }
      display.exibirSensores(ultimaLeituraSensor1, ultimaLeituraSensor2);
      
      xSemaphoreGive(xI2CMutex);  // ← Libera o mutex após usar I2C
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ==================== Tarefa do Botão ====================
void tarefaBotao(void *arg) {
  int lastButtonState = HIGH;
  unsigned long lastDebounceTime = 0;
  
  while (1) {
    esp_task_wdt_reset();
    int reading = digitalRead(PINO_BOTAO);
    
    if (reading != lastButtonState) lastDebounceTime = millis();
    
    if ((millis() - lastDebounceTime) > 50 && reading == LOW) {
      if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        configAtual.displayLigado = !configAtual.displayLigado;
        xSemaphoreGive(xConfigMutex);
        
        prefs.begin("niveltec", false);
        prefs.putBool("displayLigado", configAtual.displayLigado);
        prefs.end();
        
        if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
          if (configAtual.displayLigado) {
            display.on();
            display.printLog("Display ligado");
          } else {
            display.off();
          }
          xSemaphoreGive(xI2CMutex);
        }
        Serial.println("Display toggled via botão");
      }
    }
    lastButtonState = reading;
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ==================== Tarefa do LED ====================
void tarefaLED(void *arg) {
  while (1) {
    esp_task_wdt_reset();
    if (isConnected()) {
      digitalWrite(PINO_LED, HIGH); vTaskDelay(500 / portTICK_PERIOD_MS);
      digitalWrite(PINO_LED, LOW); vTaskDelay(500 / portTICK_PERIOD_MS);
    } else {
      digitalWrite(PINO_LED, HIGH); vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(PINO_LED, LOW); vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

// ==================== Tarefa 6: Verificação de Reconexão ====================
void tarefaVerificaConexao(void *arg) {
    bool conexaoAnterior = false;
    while (1) {
        esp_task_wdt_reset();
        bool conectado = isConnected();
        
        // Transição de desconectado para conectado
        if (conectado && !conexaoAnterior) {
            Serial.println("Conexão RESTABELECIDA!");
            if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                display.printLog("Conexão OK");
                xSemaphoreGive(xI2CMutex);
            }
            
            // Reenviar dados pendentes
            Armazenamento::enviarPendentes();
            
            // Tentar enviar DeviceInfo se ainda não foi enviado
            if (!deviceInfoEnviado) {
                if (DeviceInfo::saveToFirebase()) {
                    deviceInfoEnviado = true;
                    Serial.println("DeviceInfo enviado após reconexão");
                    if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                        display.printLog("Info enviada");
                        xSemaphoreGive(xI2CMutex);
                    }
                }
            }
            
            // Atualizar status no Firebase
            updateDeviceStatus("ativo");
            DeviceInfo::updateStatus("ativo");
        }
        
        conexaoAnterior = conectado;
        vTaskDelay(2000 / portTICK_PERIOD_MS);  // verifica a cada 2 segundos
    }
}