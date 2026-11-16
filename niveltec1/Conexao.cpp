#include "Conexao.h"
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <CRC32.h>

WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Preferences preferences;
String deviceId = "";

String generateDeviceId() {
  String macStr = WiFi.macAddress();
  macStr.replace(":", "");
  macStr.toUpperCase();

  uint8_t mac[6];
  for (int i = 0; i < 6; i++) {
    String byteStr = macStr.substring(i * 2, i * 2 + 2);
    mac[i] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
  }

  CRC32 crc;
  crc.update(mac, 6);
  uint32_t crc32 = crc.finalize();

  char crcHex[9];
  sprintf(crcHex, "%08X", crc32);

  uint8_t parity = (crc32 & 0xFF) ^ ((crc32 >> 8) & 0xFF) ^ ((crc32 >> 16) & 0xFF) ^ ((crc32 >> 24) & 0xFF);
  parity &= 0x0F;               
  char parityHex[2];
  sprintf(parityHex, "%X", parity);

  return String(crcHex) + String(parityHex);
}

bool validateDeviceId(const String& id) {
  if (id.length() != 9) return false;
  String crcPart = id.substring(0, 8);
  char parityGiven = id.charAt(8);
  uint32_t crc32 = (uint32_t)strtoul(crcPart.c_str(), NULL, 16);
  uint8_t parityCalc = (crc32 & 0xFF) ^ ((crc32 >> 8) & 0xFF) ^ ((crc32 >> 16) & 0xFF) ^ ((crc32 >> 24) & 0xFF);
  parityCalc &= 0x0F;
  char parityCalcHex[2];
  sprintf(parityCalcHex, "%X", parityCalc);
  return parityGiven == parityCalcHex[0];
}

void setupConexao() {
  Serial.println("Conectando WiFi...");
  wifiManager.setConfigPortalTimeout(180);
  if (!wifiManager.autoConnect("NivelTec_IFS")) {
    Serial.println("Falha WiFi. Reiniciando...");
    delay(3000);
    ESP.restart();
  }
  Serial.println("WiFi conectado!");

  deviceId = generateDeviceId(); 

  preferences.begin("niveltec", false);
  if (!preferences.isKey("deviceId")) {
    preferences.putString("deviceId", deviceId);
    Serial.println("Novo Device ID gerado: " + deviceId);
  } else {
    deviceId = preferences.getString("deviceId"); 
    if (!validateDeviceId(deviceId)) {
      Serial.println("Device ID inválido! Regenerando...");
      deviceId = generateDeviceId();
      preferences.putString("deviceId", deviceId);
    }
    Serial.println("Device ID carregado: " + deviceId);
  }
  preferences.end();

  timeClient.begin();
  timeClient.setTimeOffset(-3 * 3600);
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(1000);
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback; 
  config.max_token_generation_retry = 5;

  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);

  for (int i = 0; i < 10; i++) {
    if (Firebase.ready()) break;
    delay(500);
  }

  if (Firebase.ready()) {
    Serial.println("Firebase autenticado");
    updateDeviceStatus("ativo");
    
    // Testa a leitura inicial das configs
    int intv; int amos; String env; String oper; bool disp; float valX;
    if (getConfigFromFirebase(intv, amos, env, oper, disp, valX)) {
        Serial.println("✓ Configurações validadas no início.");
    }
  } else {
    Serial.println("Falha na autenticação do Firebase");
  }
}

bool isConnected() {
  return (WiFi.status() == WL_CONNECTED) && Firebase.ready();
}

unsigned long getTimestamp() {
  return timeClient.getEpochTime();
}

String getFormattedTime() {
  return timeClient.getFormattedTime();
}

String getTimeString() {
  time_t now = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&now);
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

String getDeviceId() {
  if (deviceId.length() == 0) {
    preferences.begin("niveltec", true);
    deviceId = preferences.getString("deviceId", "");
    preferences.end();
  }
  return deviceId;
}

bool sendDataToFirebase(const String &path, FirebaseJson &json) {
  if (!Firebase.ready()) return false;
  
  fbdo.clear(); 
  if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
    return true;
  } else {
    Serial.print("Erro RTDB: ");
    Serial.println(fbdo.errorReason());
    return false;
  }
}

bool getConfigFromFirebase(int &intervalo, int &amostras, String &envio, String &operacao, bool &display, float &valorX) {
  if (!Firebase.ready()) return false;
  
  String path = "/dispositivos/" + deviceId + "/config";
  
  fbdo.clear(); 
  bool gotData = Firebase.RTDB.getJSON(&fbdo, path.c_str());
  
  if (!gotData) {
    String err = fbdo.errorReason();
    int httpCode = fbdo.httpCode();
    
    // BLINDAGEM DEFINITIVA: Só cria config padrão se o Firebase confirmar que o caminho não existe (404)
    // Se for timeout, erro SSL ou erro de servidor (httpCode == 0, 500, etc), ignora e tenta depois.
    if (httpCode == 404 || err == "path not found" || err == "null") {
        Serial.println("Config não encontrada no Firebase (404). Criando padrão...");
        enviarConfigPadrao();
        delay(300);
        fbdo.clear();
        
        if (Firebase.RTDB.getJSON(&fbdo, path.c_str()) && fbdo.dataType() != "null") {
          FirebaseJson &json = fbdo.jsonObject();
          FirebaseJsonData result;
          if (json.get(result, "intervaloMedicao")) intervalo = result.to<int>();
          if (json.get(result, "amostrasMedia")) amostras = result.to<int>();
          if (json.get(result, "modoEnvio")) envio = result.to<String>();
          if (json.get(result, "modoOperacao")) operacao = result.to<String>();
          if (json.get(result, "displayLigado")) display = result.to<bool>();
          if (json.get(result, "valorX")) valorX = result.to<float>();
          return true;
        }
    } else {
        // É apenas uma falha de rede momentânea (SSL, Timeout). Apenas ignora e mantém as configs atuais.
        Serial.println("Falha de rede ao ler config. Mantendo as anteriores.");
        return false; 
    }
    return false;
  }
  
  FirebaseJson &json = fbdo.jsonObject();
  FirebaseJsonData result;
  if (json.get(result, "intervaloMedicao")) intervalo = result.to<int>();
  if (json.get(result, "amostrasMedia")) amostras = result.to<int>();
  if (json.get(result, "modoEnvio")) envio = result.to<String>();
  if (json.get(result, "modoOperacao")) operacao = result.to<String>();
  if (json.get(result, "displayLigado")) display = result.to<bool>();
  if (json.get(result, "valorX")) valorX = result.to<float>();
  
  return true;
}

void enviarConfigPadrao() {
  if (!Firebase.ready()) return;
  
  String path = "/dispositivos/" + deviceId + "/config";
  
  FirebaseJson json;
  json.set("intervaloMedicao", 600);
  json.set("amostrasMedia", 50);
  json.set("modoEnvio", "ambos");
  json.set("modoOperacao", "sempreAtivo");
  json.set("displayLigado", true);
  json.set("valorX", 29.8); 
  
  fbdo.clear();
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("✓ Config padrão criada no Firebase!");
  } else {
    Serial.println("✗ Erro ao criar config padrão: " + fbdo.errorReason());
  }
}

// ==================== ENVIO DE DADOS COMPLETO ====================
void enviarDados(float y_fusao, float sigma_y, float d1, float desv1, float d2, float desv2, unsigned long timestamp, const String &modo) {
  if (!Firebase.ready()) return;
  
  String path = "/dispositivos/" + deviceId + "/medicoes/" + String(timestamp);
  FirebaseJson json;
  
  json.set("nivel_y", y_fusao);      
  json.set("sigma_y", sigma_y);      // Incerteza final do Kalman
  json.set("sensor1_dist", d1);      
  json.set("sensor1_desv", desv1);   // Desvio padrão do S1
  json.set("sensor2_dist", d2);      
  json.set("sensor2_desv", desv2);   // Desvio padrão do S2
  json.set("timestamp", timestamp);
  
  sendDataToFirebase(path, json);
  
  String lastPath = "/dispositivos/" + deviceId + "/ultimaLeitura";
  fbdo.clear();
  Firebase.RTDB.setInt(&fbdo, lastPath, timestamp);
}

void updateDeviceStatus(const String &status) {
  if (!Firebase.ready()) return;
  String path = "/dispositivos/" + deviceId + "/estado";
  fbdo.clear();
  Firebase.RTDB.setString(&fbdo, path, status);
}

void enviarStatusCalibracao(bool calibrado) {
  if (!Firebase.ready()) return;
  String path = "/dispositivos/" + deviceId + "/calibracao";
  FirebaseJson json;
  json.set("calibrado", calibrado);
  json.set("timestamp", getTimestamp());
  
  fbdo.clear();
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
      Serial.println("Status de calibração enviado ao Firebase.");
  }
}

bool sendDeviceInfoToFirebase(const String &mac, const String &ip,
                              unsigned long firstBoot, unsigned long lastBoot,
                              unsigned long bootCount, const String &firmwareVersion,
                              const String &status) {
    if (!Firebase.ready()) {
        Serial.println("Firebase não pronto - DeviceInfo não enviado");
        return false;
    }
    
    String path = "/dispositivos/" + deviceId + "/info";
    FirebaseJson json;
    json.set("mac", mac);
    json.set("ip", ip);
    json.set("firstBoot", firstBoot);
    json.set("lastBoot", lastBoot);
    json.set("bootCount", bootCount);
    json.set("firmwareVersion", firmwareVersion);
    json.set("status", status);
    json.set("lastUpdate", getTimestamp());
    
    return sendDataToFirebase(path, json);
}