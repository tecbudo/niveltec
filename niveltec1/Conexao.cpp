#include "Conexao.h"
#include <CRC32.h>

WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600);
Preferences preferences;
String deviceId = "";

// === OBJETOS DO FIREBASECLIENT ===
WiFiClientSecure ssl_client;
void getNetworkData(String &ip, String &mac, WiFiSTAConnectStatus &status); 

AsyncClient aClient(ssl_client, getNetworkData);
FirebaseClient firebaseClient;
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
RealtimeDatabase rtbd(DATABASE_URL);

// Callback de rede (exigido pelo AsyncClient)
void getNetworkData(String &ip, String &mac, WiFiSTAConnectStatus &status) {
    ip = WiFi.localIP().toString();
    mac = WiFi.macAddress();
    status = (WiFi.status() == WL_CONNECTED) ? WiFiSTAConnectStatus::Connected : WiFiSTAConnectStatus::Disconnected;
}

// Estrutura para tornar as operações assíncronas em bloqueantes
struct FirebaseResult {
    bool done = false;
    bool success = false;
    String payload = "";
    String errorMsg = "";
};

// ... (funções generateDeviceId e validateDeviceId permanecem iguais) ...
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

bool validateDeviceId(const String& deviceId) {
    if (deviceId.length() != 9) return false;
    String crcPart = deviceId.substring(0, 8);
    char parityGiven = deviceId.charAt(8);
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
    } else {
        deviceId = preferences.getString("deviceId");
        if (!validateDeviceId(deviceId)) {
            deviceId = generateDeviceId();
            preferences.putString("deviceId", deviceId);
        }
    }
    preferences.end();
    Serial.print("Device ID: "); Serial.println(deviceId);

    timeClient.begin();
    timeClient.setTimeOffset(-3 * 3600);
    while (!timeClient.update()) {
        timeClient.forceUpdate();
        delay(1000);
    }
    Serial.println("Tempo sincronizado");

    ssl_client.setInsecure(); 
    
    // Inicia autenticação
    firebaseClient.auth(aClient, user_auth);
    
    Serial.println("Autenticando no Firebase...");
    unsigned long startAuth = millis();
    // CORREÇÃO: Usamos user_auth.isAuthenticated() em vez de firebaseClient.authenticated()
    while (!user_auth.isAuthenticated() && (millis() - startAuth < 10000)) {
        firebaseClient.loop(); 
        delay(10);
    }

    if (user_auth.isAuthenticated()) {
        Serial.println("Firebase autenticado");
        updateDeviceStatus("ativo");
        
        int intv; int amos; String env; String oper; bool disp;
        if (getConfigFromFirebase(intv, amos, env, oper, disp)) {
            Serial.println("✓ Configurações validadas no início.");
        }
    } else {
        Serial.println("Falha na autenticação do Firebase");
    }
}

bool isConnected() {
    // CORREÇÃO: user_auth.isAuthenticated()
    return (WiFi.status() == WL_CONNECTED) && user_auth.isAuthenticated();
}

unsigned long getTimestamp() { return timeClient.getEpochTime(); }
String getFormattedTime() { return timeClient.getFormattedTime(); }

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

// === WRAPPERS COM CALLBACKS CORRIGIDOS (AsyncResult) ===

bool sendDataToFirebase(const String &path, FirebaseJson &json) {
    if (!user_auth.isAuthenticated()) return false;
    
    FirebaseResult res;
    // CORREÇÃO: O callback agora recebe AsyncResult &result
    rtbd.set(aClient, path, json, [&res](AsyncResult &result) {
        res.done = true; 
        res.success = result.success();
    }, [&res](FirebaseError error) {
        res.done = true; 
        res.success = false; 
        res.errorMsg = error.message();
    });

    unsigned long start = millis();
    while (!res.done && (millis() - start < 5000)) { firebaseClient.loop(); delay(5); }
    
    if (!res.success) Serial.print("Erro RTDB: ") + res.errorMsg;
    return res.success;
}

bool getConfigFromFirebase(int &intervalo, int &amostras, String &envio, String &operacao, bool &display) {
    if (!user_auth.isAuthenticated()) return false;
    String path = "/dispositivos/" + deviceId + "/config";
    
    FirebaseResult res;
    rtbd.get(aClient, path, [&res](AsyncResult &result) {
        res.done = true; 
        res.success = result.success();
        res.payload = result.c_str(); // Pega o payload como string
    }, [&res](FirebaseError error) {
        res.done = true; 
        res.success = false; 
        res.errorMsg = error.message();
    });

    unsigned long start = millis();
    while (!res.done && (millis() - start < 5000)) { firebaseClient.loop(); delay(5); }

    if (!res.success || res.payload == "null" || res.payload == "") {
        Serial.println("Config não encontrada no Firebase. Criando padrão...");
        enviarConfigPadrao();
        delay(500); 
        
        res.done = false; res.success = false; res.payload = "";
        rtbd.get(aClient, path, [&res](AsyncResult &result) {
            res.done = true; 
            res.success = result.success();
            res.payload = result.c_str();
        }, [&res](FirebaseError error) { 
            res.done = true; 
            res.success = false; 
        });
        
        start = millis();
        while (!res.done && (millis() - start < 5000)) { firebaseClient.loop(); delay(5); }
        if (!res.success) return false;
    }

    FirebaseJson json;
    json.setJsonData(res.payload);
    FirebaseJsonData result;
    if (json.get(result, "intervaloMedicao")) intervalo = result.to<int>();
    if (json.get(result, "amostrasMedia")) amostras = result.to<int>();
    if (json.get(result, "modoEnvio")) envio = result.to<String>();
    if (json.get(result, "modoOperacao")) operacao = result.to<String>();
    if (json.get(result, "displayLigado")) display = result.to<bool>();
    
    return true;
}

void enviarConfigPadrao() {
    if (!user_auth.isAuthenticated()) return;
    String path = "/dispositivos/" + deviceId + "/config";
    FirebaseJson json;
    json.set("intervaloMedicao", 600);
    json.set("amostrasMedia", 5);
    json.set("modoEnvio", "ambos");
    json.set("modoOperacao", "sempreAtivo");
    json.set("displayLigado", true);
    sendDataToFirebase(path, json);
}

void enviarDados(float d1, float d2, float media, const String &modo) {
    if (!user_auth.isAuthenticated()) return;
    unsigned long now = getTimestamp();
    String path = "/dispositivos/" + deviceId + "/medicoes/" + String(now);
    FirebaseJson json;
    json.set("sensor1_dist", d1);
    json.set("sensor2_dist", d2);
    json.set("media", media);
    json.set("timestamp", now);
    sendDataToFirebase(path, json);

    String lastPath = "/dispositivos/" + deviceId + "/ultimaLeitura";
    FirebaseResult res;
    rtbd.set(aClient, lastPath, (int)now, [&res](AsyncResult &result) {
        res.done = true; res.success = result.success();
    }, [&res](FirebaseError error) { 
        res.done = true; res.success = false; 
    });
    while (!res.done && user_auth.isAuthenticated()) { firebaseClient.loop(); delay(5); }
}

void updateDeviceStatus(const String &status) {
    if (!user_auth.isAuthenticated()) return;
    String path = "/dispositivos/" + deviceId + "/estado";
    FirebaseResult res;
    rtbd.set(aClient, path, status, [&res](AsyncResult &result) {
        res.done = true; res.success = result.success();
    }, [&res](FirebaseError error) { 
        res.done = true; res.success = false; 
    });
    while (!res.done && user_auth.isAuthenticated()) { firebaseClient.loop(); delay(5); }
}

bool sendDeviceInfoToFirebase(const String &mac, const String &ip,
                              unsigned long firstBoot, unsigned long lastBoot,
                              unsigned long bootCount, const String &firmwareVersion,
                              const String &status) {
    if (!user_auth.isAuthenticated()) {
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