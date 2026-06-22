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
  // Obtém o MAC como string sem separadores
  String macStr = WiFi.macAddress();
  macStr.replace(":", "");
  macStr.toUpperCase();

  // Converte a string MAC para um array de bytes
  uint8_t mac[6];
  for (int i = 0; i < 6; i++) {
    String byteStr = macStr.substring(i * 2, i * 2 + 2);
    mac[i] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
  }

  // Calcula o CRC-32 dos 6 bytes do MAC
  CRC32 crc;
  crc.update(mac, 6);
  uint32_t crc32 = crc.finalize();

  // Formata os 32 bits do CRC como 8 caracteres hex
  char crcHex[9];
  sprintf(crcHex, "%08X", crc32);

  // Calcula a paridade (XOR dos 4 bytes do CRC) e mantém 4 bits
  uint8_t parity = (crc32 & 0xFF) ^ ((crc32 >> 8) & 0xFF) ^ ((crc32 >> 16) & 0xFF) ^ ((crc32 >> 24) & 0xFF);
  parity &= 0x0F;               // mantém apenas os 4 bits menos significativos
  char parityHex[2];
  sprintf(parityHex, "%X", parity);

  // Concatena CRC + paridade
  return String(crcHex) + String(parityHex);
}

bool validateDeviceId(const String& deviceId) {
  if (deviceId.length() != 9) return false;

  String crcPart = deviceId.substring(0, 8);
  char parityGiven = deviceId.charAt(8);

  // Converte a parte CRC para um valor de 32 bits
  uint32_t crc32 = (uint32_t)strtoul(crcPart.c_str(), NULL, 16);

  // Calcula a paridade esperada
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

   // Gera/recupera ID único do dispositivo (baseado no MAC)
  deviceId = generateDeviceId();   // <-- agora usa a variável global

  preferences.begin("niveltec", false);
  if (!preferences.isKey("deviceId")) {
    preferences.putString("deviceId", deviceId);
    Serial.println("Novo Device ID gerado: " + deviceId);
  } else {
    deviceId = preferences.getString("deviceId");  // <-- atualiza a global
    // Valida o ID armazenado; se inválido, regenera
    if (!validateDeviceId(deviceId)) {
      Serial.println("Device ID inválido! Regenerando...");
      deviceId = generateDeviceId();
      preferences.putString("deviceId", deviceId);
    }
    Serial.println("Device ID carregado: " + deviceId);
  }
  preferences.end();
  Serial.print("Device ID: "); Serial.println(deviceId);

  // Sincroniza horário via NTP
  timeClient.begin();
  timeClient.setTimeOffset(-3 * 3600);
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(1000);
  }
  Serial.println("Tempo sincronizado");

  // Configura Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback; // exibe logs do token 
  config.max_token_generation_retry = 5;

  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);

  // Aguarda autenticação (máx 5 segundos)
  for (int i = 0; i < 10; i++) {
    if (Firebase.ready()) break;
    delay(500);
  }

  if (Firebase.ready()) {
    Serial.println("Firebase autenticado");
    updateDeviceStatus("ativo");
    // FORÇA a verificação/criação de configuração logo no boot
    int intv; int amos; String env; String oper; bool disp;
    if (getConfigFromFirebase(intv, amos, env, oper, disp)) {
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
  // Constrói a data/hora a partir do epoch
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
  if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
    return true;
  } else {
    Serial.print("Erro RTDB: ");
    Serial.println(fbdo.errorReason());
    return false;
  }
}
bool getConfigFromFirebase(int &intervalo, int &amostras, String &envio, String &operacao, bool &display) {
  if (!Firebase.ready()) return false;
  
  String path = "/dispositivos/" + deviceId + "/config";
  
  // Tenta buscar o JSON
  bool gotData = Firebase.RTDB.getJSON(&fbdo, path.c_str());
  
  // CASO 1: Nó não existe (404) ou retornou null → cria config padrão
  if (!gotData || fbdo.dataType() == "null") {
    Serial.println("Config não encontrada no Firebase. Criando padrão...");
    
    // Cria a estrutura inicial
    enviarConfigPadrao();
    
    // Aguarda propagação no Firebase (crítico!)
    delay(300);
    
    // Tenta ler novamente após criar
    if (Firebase.RTDB.getJSON(&fbdo, path.c_str()) && fbdo.dataType() != "null") {
      FirebaseJson &json = fbdo.jsonObject();
      FirebaseJsonData result;
      
      if (json.get(result, "intervaloMedicao")) intervalo = result.to<int>();
      if (json.get(result, "amostrasMedia")) amostras = result.to<int>();
      if (json.get(result, "modoEnvio")) envio = result.to<String>();
      if (json.get(result, "modoOperacao")) operacao = result.to<String>();
      if (json.get(result, "displayLigado")) display = result.to<bool>();
      
      Serial.println("✓ Config padrão lida com sucesso após criação");
      return true;
    }
    
    Serial.println("⚠️ Falha ao ler config após criação");
    return false;
  }
  
  // CASO 2: Dados encontrados → lê os campos normalmente
  FirebaseJson &json = fbdo.jsonObject();
  FirebaseJsonData result;
  
  if (json.get(result, "intervaloMedicao")) intervalo = result.to<int>();
  if (json.get(result, "amostrasMedia")) amostras = result.to<int>();
  if (json.get(result, "modoEnvio")) envio = result.to<String>();
  if (json.get(result, "modoOperacao")) operacao = result.to<String>();
  if (json.get(result, "displayLigado")) display = result.to<bool>();
  
  return true;
}

void enviarConfigPadrao() {
  if (!Firebase.ready()) return;
  
  String path = "/dispositivos/" + deviceId + "/config";
  
  FirebaseJson json;
  json.set("intervaloMedicao", 600);
  json.set("amostrasMedia", 5);
  json.set("modoEnvio", "ambos");
  json.set("modoOperacao", "sempreAtivo");
  json.set("displayLigado", true);
  
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("✓ Config padrão criada no Firebase!");
  } else {
    Serial.println("✗ Erro: " + fbdo.errorReason());
  }
}

void enviarDados(float d1, float d2, float media, const String &modo) {
  if (!Firebase.ready()) return;
  unsigned long now = getTimestamp();
  String path = "/dispositivos/" + deviceId + "/medicoes/" + String(now);
  FirebaseJson json;
  json.set("sensor1_dist", d1);
  json.set("sensor2_dist", d2);
  json.set("media", media);
  json.set("timestamp", now);
  if (modo == "bruto" || modo == "ambos") {
    // Opcional: adicionar array de amostras brutas
  }
  sendDataToFirebase(path, json);
  // Atualiza timestamp da última leitura
  String lastPath = "/dispositivos/" + deviceId + "/ultimaLeitura";
  Firebase.RTDB.setInt(&fbdo, lastPath, now);
}

void updateDeviceStatus(const String &status) {
  if (!Firebase.ready()) return;
  String path = "/dispositivos/" + deviceId + "/estado";
  Firebase.RTDB.setString(&fbdo, path, status);
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
