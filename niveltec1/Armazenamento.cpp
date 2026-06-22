#include "Armazenamento.h"
#include "Conexao.h"    // para isConnected() e enviarDados()
#include <ArduinoJson.h>
SemaphoreHandle_t xPendentesMutex = NULL;

bool Armazenamento::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("Falha ao montar LittleFS");
        return false;
    }
    xPendentesMutex = xSemaphoreCreateMutex();
    Serial.println("LittleFS montado com sucesso");
    return true;
}

bool Armazenamento::salvarPendente(float d1, float d2, float media, unsigned long timestamp, const String& modo) {
    // Abrir arquivo existente para leitura
    File file = LittleFS.open("/pendentes.json", "r");
    DynamicJsonDocument doc(8192);  // Capacidade para ~50 pendentes (cada ~150 bytes)
    if (file) {
        DeserializationError error = deserializeJson(doc, file);
        if (error) {
            Serial.println("Erro ao ler pendentes.json, recriando arquivo");
            doc.clear();
        }
        file.close();
    }
    
    // Obter o array (se não existir, criar)
    JsonArray array = doc.as<JsonArray>();
    if (array.isNull()) {
        array = doc.to<JsonArray>();
    }
    
    // Adicionar novo pendente
    JsonObject novo = array.createNestedObject();
    novo["timestamp"] = timestamp;
    novo["s1"] = d1;
    novo["s2"] = d2;
    novo["media"] = media;
    novo["modo"] = modo;
    
    // Salvar arquivo
    file = LittleFS.open("/pendentes.json", "w");
    if (!file) {
        Serial.println("Falha ao abrir pendentes.json para escrita");
        return false;
    }
    serializeJson(doc, file);
    file.close();
    Serial.printf("Pendente salvo: timestamp=%u, s1=%.1f, s2=%.1f\n", timestamp, d1, d2);
    return true;
}

void Armazenamento::enviarPendentes() {
    // Protege contra execução simultânea
    if (xSemaphoreTake(xPendentesMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("Timeout ao tentar enviar pendentes (mutex ocupado)");
        return;
    }

    if (!temPendentes()) {
        xSemaphoreGive(xPendentesMutex);
        return;
    }

    File file = LittleFS.open("/pendentes.json", "r");
    if (!file) {
        xSemaphoreGive(xPendentesMutex);
        return;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.println("Erro ao desserializar pendentes.json, ignorando...");
        xSemaphoreGive(xPendentesMutex);
        return;
    }

    JsonArray array = doc.as<JsonArray>();
    if (array.isNull()) {
        xSemaphoreGive(xPendentesMutex);
        return;
    }

    JsonArray novosPendentes = doc.createNestedArray();
    bool algumSucesso = false;

    for (JsonObject item : array) {
        unsigned long ts = item["timestamp"];
        float s1 = item["s1"];
        float s2 = item["s2"];
        float media = item["media"];
        String modo = item["modo"].as<String>();

        // Validação: timestamp > 0 e valores plausíveis (ex: entre 0 e 5000 mm)
        if (ts == 0 || s1 < 0 || s2 < 0 || media < 0) {
            Serial.printf("Pendente inválido ignorado: ts=%lu, s1=%.1f, s2=%.1f\n", ts, s1, s2);
            continue; // descarta esse pendente corrompido
        }

        if (isConnected()) {
            // Tenta enviar
            enviarDados(s1, s2, media, modo);
            Serial.printf("Pendente reenviado: timestamp=%lu\n", ts);
            algumSucesso = true;
        } else {
            // Mantém o pendente
            JsonObject copia = novosPendentes.createNestedObject();
            copia["timestamp"] = ts;
            copia["s1"] = s1;
            copia["s2"] = s2;
            copia["media"] = media;
            copia["modo"] = modo;
        }
    }

    if (algumSucesso) {
        // Salva apenas os pendentes que ainda não foram enviados
        file = LittleFS.open("/pendentes.json", "w");
        if (file) {
            serializeJson(novosPendentes, file);
            file.close();
            Serial.printf("Arquivo de pendentes atualizado (%d restantes)\n", novosPendentes.size());
        } else {
            Serial.println("Erro ao salvar pendentes atualizados!");
        }
    }

    xSemaphoreGive(xPendentesMutex);
}

bool Armazenamento::temPendentes() {
    File file = LittleFS.open("/pendentes.json", "r");
    if (!file) return false;
    size_t size = file.size();
    file.close();
    return size > 0;
}

void Armazenamento::limparPendentes() {
    if (LittleFS.exists("/pendentes.json")) {
        LittleFS.remove("/pendentes.json");
        Serial.println("Todos os pendentes foram removidos");
    }
}