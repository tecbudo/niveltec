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
#include "Calibracao.h"

// ==================== Definições de Pinos ====================
#define PINO_LED        15
#define PINO_BOTAO      0
#define I2C_SDA         33
#define I2C_SCL         35
#define XSHUT_SENSOR1   39
#define XSHUT_SENSOR2   37

// ==================== Estrutura de Configuração ====================
struct Config {
  int intervaloMedicao = 10;
  int amostrasMedia = 50;
  String modoEnvio = "ambos";
  String modoOperacao = "sempreAtivo";
  bool displayLigado = true;
  float valorX = 29.8; 
};

// ==================== Variáveis Globais ====================
Config configAtual;
Preferences prefs;
SemaphoreHandle_t xConfigMutex;
SemaphoreHandle_t xDisplayMutex;
SemaphoreHandle_t xI2CMutex;

float ultimaLeituraSensor1 = 0;
float ultimaLeituraSensor2 = 0;
float ultimaMedia = 0; 
bool deviceInfoEnviado = false;

// ==================== Máquina de Estados da Medição ====================
enum EstadoMedicao {
  IDLE,                 
  COLETANDO_BASE,       
  COLETANDO_NIVEL_X,    
  MEDINDO               
};

EstadoMedicao estadoAtual = IDLE;
unsigned long tempoUltimaMedicao = 0;
String comandoCalibracaoFirebase = "nenhum"; 

// Acumuladores da Máquina de Estados
int amostrasColetadas = 0;
float soma1 = 0, soma2 = 0, somaQ1 = 0, somaQ2 = 0;

// ==================== Protótipos ====================
void tarefaComunicacao(void *arg);
void tarefaMedicao(void *arg);
void tarefaDisplay(void *arg);
void tarefaBotao(void *arg);
void tarefaLED(void *arg);
void tarefaVerificaConexao(void *arg);
void scanI2C();
float calcularMediana(float *arr, int n);
bool lerMedianaSensores(float &med1, float &med2);

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Iniciando sistema de monitoramento...");

  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(XSHUT_SENSOR1, OUTPUT);
  pinMode(XSHUT_SENSOR2, OUTPUT);
  
  digitalWrite(XSHUT_SENSOR1, LOW);
  digitalWrite(XSHUT_SENSOR2, LOW);
  delay(10);
  digitalWrite(XSHUT_SENSOR1, HIGH);
  digitalWrite(XSHUT_SENSOR2, HIGH);
  delay(10);

  Wire.begin(I2C_SDA, I2C_SCL);
  delay(200);
  scanI2C();
  delay(1000);

  if (!Armazenamento::begin()) {
      Serial.println("Aviso: Falha no LittleFS. Dados offline não serão salvos.");
  }

  display.begin();
  if (display.isDisponivel()) {
    display.clear();
    display.print("INICIANDO...");
    display.setStatus(1);
  }

  Calibracao::init();
  if (Calibracao::isCalibrado()) {
      Serial.println("Sistema CALIBRADO e pronto para medir.");
      if (display.isDisponivel()) display.printLog("Sistema Calibrado");
  } else {
      Serial.println("Aviso: Sistema NAO calibrado.");
      if (display.isDisponivel()) display.printLog("Falta Calibrar");
  }

  prefs.begin("niveltec", false);
  configAtual.intervaloMedicao = prefs.getInt("intervalo", 10);
  configAtual.amostrasMedia = prefs.getInt("amostras", 50);
  configAtual.modoEnvio = prefs.getString("modoEnvio", "ambos");
  configAtual.modoOperacao = prefs.getString("modoOperacao", "sempreAtivo");
  configAtual.displayLigado = prefs.getBool("displayLigado", true);
  configAtual.valorX = prefs.getFloat("valorX", 29.8);
  prefs.end();

  if (configAtual.displayLigado && display.isDisponivel()) display.on();
  else if (display.isDisponivel()) display.off();

  xConfigMutex = xSemaphoreCreateMutex();
  xDisplayMutex = xSemaphoreCreateMutex();
  xI2CMutex = xSemaphoreCreateMutex();

  sensores.iniciar(XSHUT_SENSOR1, XSHUT_SENSOR2);
  Serial.println("Sensores inicializados");

  setupConexao();
  Serial.println("Conexões inicializadas");
  updateDeviceStatus("ativo");
  
  DeviceInfo::init();

  if (isConnected()) {
      if (DeviceInfo::saveToFirebase()) {
          deviceInfoEnviado = true;
      }
      Armazenamento::enviarPendentes();
  }

  tempoUltimaMedicao = millis(); 

  xTaskCreate(tarefaComunicacao, "Comunicacao", 8192, NULL, 2, NULL);
  xTaskCreate(tarefaMedicao, "Medicao", 8192, NULL, 1, NULL);
  xTaskCreate(tarefaDisplay, "Display", 4096, NULL, 1, NULL);
  xTaskCreate(tarefaBotao, "Botao", 2048, NULL, 1, NULL);
  xTaskCreate(tarefaLED, "LED", 2048, NULL, 1, NULL);
  xTaskCreate(tarefaVerificaConexao, "VerifConex", 8192, NULL, 1, NULL);

  Serial.println("Sistema pronto");
}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void scanI2C() {
  Serial.println("=== Escaneando I2C ===");
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) { Serial.printf("Dispositivo em 0x%02X\n", addr); count++; }
    delay(10);
  }
  if (count == 0) Serial.println("Nenhum dispositivo detectado!");
  Serial.println("=== Fim do scan ===");
}

// Função auxiliar de ordenação
float calcularMediana(float *arr, int n) {
  float tmp[5];
  for (int i = 0; i < n; i++) tmp[i] = arr[i];
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (tmp[i] > tmp[j]) { float aux = tmp[i]; tmp[i] = tmp[j]; tmp[j] = aux; }
    }
  }
  return tmp[n / 2];
}

// NOVA FUNÇÃO: Lê 5 valores em sequência e retorna a mediana
bool lerMedianaSensores(float &med1, float &med2) {
  float arr1[5], arr2[5];
  int validos = 0;

  for (int i = 0; i < 5; i++) {
    float d1 = -1, d2 = -1;
    if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      d1 = sensores.lerDistancia(0);
      d2 = sensores.lerDistancia(1);
      xSemaphoreGive(xI2CMutex);
    }

    if (d1 > 10 && d1 < 2000 && d2 > 10 && d2 < 2000) {
      arr1[validos] = d1;
      arr2[validos] = d2;
      validos++;
    }
    
    // Cede o processador por 50ms, permitindo outras tarefas rodarem
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }

  if (validos < 3) return false; // Falha se não tiver pelo menos 3 leituras boas

  med1 = calcularMediana(arr1, validos);
  med2 = calcularMediana(arr2, validos);
  return true;
}

// ==================== Tarefa de Comunicação ====================
void tarefaComunicacao(void *arg) {
  while (1) {
    esp_task_wdt_reset();
    if (isConnected()) {
      int intervalo, amostras;
      String modoEnvio, modoOperacao;
      bool displayLigado;
      float valorX;
      
      if (getConfigFromFirebase(intervalo, amostras, modoEnvio, modoOperacao, displayLigado, valorX)) {
        bool mudou = false;
        if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
          if (intervalo != configAtual.intervaloMedicao) { configAtual.intervaloMedicao = intervalo; mudou = true; }
          if (amostras != configAtual.amostrasMedia) { configAtual.amostrasMedia = amostras; mudou = true; }
          if (modoEnvio != configAtual.modoEnvio) { configAtual.modoEnvio = modoEnvio; mudou = true; }
          if (modoOperacao != configAtual.modoOperacao) { configAtual.modoOperacao = modoOperacao; mudou = true; }
          if (displayLigado != configAtual.displayLigado) { configAtual.displayLigado = displayLigado; mudou = true; }
          if (valorX != configAtual.valorX) { configAtual.valorX = valorX; mudou = true; }
          xSemaphoreGive(xConfigMutex);
        }
        
        if (mudou) {
          prefs.begin("niveltec", false);
          prefs.putInt("intervalo", configAtual.intervaloMedicao);
          prefs.putInt("amostras", configAtual.amostrasMedia);
          prefs.putString("modoEnvio", configAtual.modoEnvio);
          prefs.putString("modoOperacao", configAtual.modoOperacao);
          prefs.putBool("displayLigado", configAtual.displayLigado);
          prefs.putFloat("valorX", configAtual.valorX);
          prefs.end();
        }
      }

      String pathCmd = "/dispositivos/" + getDeviceId() + "/comando_calibracao";
      fbdo.clear();
      if (Firebase.RTDB.getString(&fbdo, pathCmd.c_str())) {
          if (fbdo.dataType() == "string") {
              String cmd = fbdo.stringData();
              if (cmd == "base" || cmd == "nivel_x" || cmd == "reset") {
                  if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                      comandoCalibracaoFirebase = cmd; 
                      xSemaphoreGive(xConfigMutex);
                  }
                  Firebase.RTDB.setString(&fbdo, pathCmd.c_str(), "nenhum");
                  Serial.println("Comando recebido: " + cmd);
              } else if (cmd != "nenhum" && cmd != "") {
                  Firebase.RTDB.setString(&fbdo, pathCmd.c_str(), "nenhum");
              }
          }
      }
    }
    esp_task_wdt_reset(); 
    vTaskDelay(5000 / portTICK_PERIOD_MS); 
  }
}

// ==================== Tarefa de Medição (Lógica Simplificada) ====================
void tarefaMedicao(void *arg) {
  Serial.println(">>> [MED] Tarefa iniciada");
  esp_task_wdt_reset();
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  
  while (1) {
    esp_task_wdt_reset();
    
    int intervalo = 10;
    int amostras = 50;
    if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      intervalo = configAtual.intervaloMedicao;
      amostras = configAtual.amostrasMedia;
      xSemaphoreGive(xConfigMutex);
    }

    // Verifica comandos
    if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (comandoCalibracaoFirebase == "base") {
            estadoAtual = COLETANDO_BASE;
            amostrasColetadas = 0; soma1 = soma2 = somaQ1 = somaQ2 = 0;
        } else if (comandoCalibracaoFirebase == "nivel_x") {
            estadoAtual = COLETANDO_NIVEL_X;
            amostrasColetadas = 0; soma1 = soma2 = somaQ1 = somaQ2 = 0;
        } else if (comandoCalibracaoFirebase == "reset") {
            Calibracao::reset();
            enviarStatusCalibracao(false);
            if (display.isDisponivel()) display.printLog("Calibracao Resetada");
        }
        comandoCalibracaoFirebase = "nenhum"; 
        xSemaphoreGive(xConfigMutex);
    }

    switch (estadoAtual) {
      case IDLE:
        if (millis() - tempoUltimaMedicao >= (intervalo * 1000UL)) {
            estadoAtual = MEDINDO;
            amostrasColetadas = 0; soma1 = soma2 = somaQ1 = somaQ2 = 0;
            if (display.isDisponivel()) display.printLog("Medindo...");
        }
        break;

      case COLETANDO_BASE:
      case COLETANDO_NIVEL_X:
      case MEDINDO:
        float med1, med2;
        // Lê a mediana das 5 leituras num único passo (leva ~250ms)
        if (lerMedianaSensores(med1, med2)) {
            soma1 += med1; somaQ1 += (med1 * med1);
            soma2 += med2; somaQ2 += (med2 * med2);
            amostrasColetadas++;
        }

        // Quando atinge o número de amostras desejado
        if (amostrasColetadas >= amostras) {
            float media1 = soma1 / amostras;
            float media2 = soma2 / amostras;
            float var1 = (somaQ1 - soma1 * soma1 / amostras) / (amostras - 1);
            float var2 = (somaQ2 - soma2 * soma2 / amostras) / (amostras - 1);
            float desv1 = (var1 > 0) ? sqrt(var1) : 0.0;
            float desv2 = (var2 > 0) ? sqrt(var2) : 0.0;

            ultimaLeituraSensor1 = media1;
            ultimaLeituraSensor2 = media2;

            if (estadoAtual == COLETANDO_BASE) {
                Calibracao::setBase(media1, desv1, media2, desv2);
                enviarStatusCalibracao(Calibracao::isCalibrado());
                Serial.println("Base calibrada com sucesso!");
                estadoAtual = IDLE; tempoUltimaMedicao = millis();
            } else if (estadoAtual == COLETANDO_NIVEL_X) {
                Calibracao::setNivelX(media1, desv1, media2, desv2, configAtual.valorX);
                enviarStatusCalibracao(Calibracao::isCalibrado());
                Serial.println("Nivel X calibrado com sucesso!");
                estadoAtual = IDLE; tempoUltimaMedicao = millis();
            } else if (estadoAtual == MEDINDO) {
                ResultadoFusao res = Calibracao::calcularY(media1, desv1, media2, desv2, amostras);
                
                if (res.valido) {
                    ultimaMedia = res.y_fusao;
                    Serial.printf("Medicao: y_fusao = %.3f mm (+- %.3f)\n", res.y_fusao, res.sigma_fusao);
                    if (isConnected()) {
                        enviarDados(res.y_fusao, res.sigma_fusao, media1, desv1, media2, desv2, getTimestamp(), configAtual.modoEnvio);
                    } else {
                        Armazenamento::salvarPendente(res.y_fusao, res.sigma_fusao, media1, desv1, media2, desv2, getTimestamp(), configAtual.modoEnvio);
                    }
                } else {
                    ultimaMedia = (media1 + media2) / 2;
                    if (isConnected()) enviarDados(ultimaMedia, 0, media1, desv1, media2, desv2, getTimestamp(), configAtual.modoEnvio);
                }
                estadoAtual = IDLE; tempoUltimaMedicao = millis(); 
            }
        }
        break;
    }

    // Loop da máquina de estados (se não estiver medindo, roda a cada 50ms)
    vTaskDelay(50 / portTICK_PERIOD_MS); 
  }
}

// ==================== Outras Tarefas (Display, Botão, LED, Verificação) ====================
void tarefaDisplay(void *arg) {
  while (1) {
    esp_task_wdt_reset();
    bool displayLigado;
    if (xSemaphoreTake(xConfigMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      displayLigado = configAtual.displayLigado;
      xSemaphoreGive(xConfigMutex);
    } else { displayLigado = true; }
    
    if (displayLigado && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      display.showTime(getFormattedTime());
      display.setStatus(isConnected() ? 2 : 1);
      display.exibirNivel(ultimaMedia > 0 ? ultimaMedia : 0, " mm");
      display.exibirSensores(ultimaLeituraSensor1, ultimaLeituraSensor2);
      xSemaphoreGive(xI2CMutex);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

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
          if (configAtual.displayLigado) { display.on(); display.printLog("Display ligado"); } else { display.off(); }
          xSemaphoreGive(xI2CMutex);
        }
      }
    }
    lastButtonState = reading;
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

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

void tarefaVerificaConexao(void *arg) {
    bool conexaoAnterior = false;
    while (1) {
        esp_task_wdt_reset();
        bool conectado = isConnected();
        if (conectado && !conexaoAnterior) {
            Serial.println("Conexão RESTABELECIDA!");
            if (display.isDisponivel() && xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                display.printLog("Conexão OK"); xSemaphoreGive(xI2CMutex);
            }
            Armazenamento::enviarPendentes();
            if (!deviceInfoEnviado) { if (DeviceInfo::saveToFirebase()) deviceInfoEnviado = true; }
            updateDeviceStatus("ativo");
            DeviceInfo::updateStatus("ativo");
            enviarStatusCalibracao(Calibracao::isCalibrado());
        }
        conexaoAnterior = conectado;
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}