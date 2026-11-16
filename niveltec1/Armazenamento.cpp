#include "Armazenamento.h"
#include "Conexao.h"    // Necessário para isConnected() e enviarDados()
#include <ArduinoJson.h>

SemaphoreHandle_t xPendentesMutex = NULL;
static bool fs_mounted = false; // Flag para saber se o FS está disponível

bool Armazenamento::begin() {
    // 1. CRIA O MUTEX PRIMEIRO (Evita Kernel Panic se o FS falhar)
    if (xPendentesMutex == NULL) {
        xPendentesMutex = xSemaphoreCreateMutex();
        if (xPendentesMutex == NULL) {
            Serial.println("Erro crítico: não foi possível criar o Mutex de pendentes");
            return false;
        }
    }

    // 2. Tenta montar o LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("Falha ao montar LittleFS. Operando sem armazenamento de pendentes.");
        fs_mounted = false;
        return false;
    }
    
    Serial.println("LittleFS montado com sucesso.");
    fs_mounted = true;
    return true;
}

// ATUALIZADO: Recebe todos os parâmetros e salva no JSON
bool Armazenamento::salvarPendente(float y_fusao, float sigma_y, float d1, float desv1, float d2, float desv2, unsigned long timestamp, const String& modo) {
    if (!fs_mounted || xPendentesMutex == NULL) {
        Serial.println("Aviso: LittleFS indisponivel. Pendente nao salvo.");
        return false;
    }

    if (xSemaphoreTake(xPendentesMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        Serial.println("Aviso: Timeout ao tentar salvar pendente.");
        return false;
    }

    File file = LittleFS.open("/pendentes.json", "r");
    DynamicJsonDocument doc(16384); 
    
    if (file) {
        DeserializationError error = deserializeJson(doc, file);
        if (error) {
            Serial.println("Arquivo pendentes.json corrompido. Recriando...");
            doc.clear();
        }
        file.close();
    }
    
    JsonArray array = doc.as<JsonArray>();
    if (array.isNull()) {
        array = doc.to<JsonArray>();
    }
    
    // Adiciona o novo pendente com TODOS os campos
    JsonObject novo = array.createNestedObject();
    novo["timestamp"] = timestamp;
    novo["y"] = y_fusao;  
    novo["sigma_y"] = sigma_y;
    novo["s1"] = d1;      
    novo["s1_desv"] = desv1;
    novo["s2"] = d2;      
    novo["s2_desv"] = desv2;
    novo["modo"] = modo;
    
    file = LittleFS.open("/pendentes.json", "w");
    if (!file) {
        Serial.println("Erro: Falha ao abrir pendentes.json para escrita.");
        xSemaphoreGive(xPendentesMutex);
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    xSemaphoreGive(xPendentesMutex);
    Serial.printf("Pendente salvo: ts=%lu, y=%.1f, s1=%.1f, s2=%.1f\n", timestamp, y_fusao, d1, d2);
    return true;
}

void Armazenamento::enviarPendentes() {
    if (!fs_mounted || xPendentesMutex == NULL) return;

    if (xSemaphoreTake(xPendentesMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("Timeout ao tentar enviar pendentes.");
        return;
    }

    File file = LittleFS.open("/pendentes.json", "r");
    if (!file) {
        xSemaphoreGive(xPendentesMutex);
        return; 
    }

    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("Erro ao ler pendentes.json. Deletando arquivo corrompido.");
        LittleFS.remove("/pendentes.json");
        xSemaphoreGive(xPendentesMutex);
        return;
    }

    JsonArray array = doc.as<JsonArray>();
    if (array.isNull() || array.size() == 0) {
        xSemaphoreGive(xPendentesMutex);
        return;
    }

    bool houveAlteracao = false;

    // Itera de trás para frente para remover os itens com segurança
    for (int i = array.size() - 1; i >= 0; i--) {
        JsonObject item = array[i];
        unsigned long ts = item["timestamp"];
        float y_val = item["y"];
        float sigma = item["sigma_y"];
        float s1 = item["s1"];
        float d1 = item["s1_desv"];
        float s2 = item["s2"];
        float d2 = item["s2_desv"];
        String modo = item["modo"].as<String>();

        // Validação de segurança
        if (ts == 0 || s1 < 0 || s2 < 0 || y_val < 0) {
            Serial.printf("Pendente inválido removido: ts=%lu\n", ts);
            array.remove(i);
            houveAlteracao = true;
            continue; 
        }

        if (isConnected()) {
            // Envia todos os dados para o Firebase
            enviarDados(y_val, sigma, s1, d1, s2, d2, ts, modo);
            Serial.printf("Pendente reenviado: ts=%lu\n", ts);
            
            array.remove(i);
            houveAlteracao = true;
            
            vTaskDelay(pdMS_TO_TICKS(100)); 
        } else {
            break; // Se a conexão cair, para de tentar e espera o próximo ciclo
        }
    }

    if (houveAlteracao) {
        if (array.size() > 0) {
            file = LittleFS.open("/pendentes.json", "w");
            if (file) {
                serializeJson(doc, file);
                file.close();
                Serial.printf("Arquivo atualizado. Restam %d pendentes.\n", array.size());
            }
        } else {
            LittleFS.remove("/pendentes.json");
            Serial.println("Todos os pendentes foram enviados. Arquivo removido.");
        }
    }

    xSemaphoreGive(xPendentesMutex);
}

bool Armazenamento::temPendentes() {
    if (!fs_mounted) return false;
    File file = LittleFS.open("/pendentes.json", "r");
    if (!file) return false;
    size_t size = file.size();
    file.close();
    return size > 0;
}

void Armazenamento::limparPendentes() {
    if (!fs_mounted || xPendentesMutex == NULL) return;

    if (xSemaphoreTake(xPendentesMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        if (LittleFS.exists("/pendentes.json")) {
            LittleFS.remove("/pendentes.json");
            Serial.println("Todos os pendentes foram removidos manualmente.");
        }
        xSemaphoreGive(xPendentesMutex);
    }
}