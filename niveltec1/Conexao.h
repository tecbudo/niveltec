#ifndef CONEXAO_H
#define CONEXAO_H

#include <WiFiManager.h>          // Gerenciador de WiFi (versão padrão)
#include <Firebase_ESP_Client.h>  // Biblioteca do Firebase
#include <NTPClient.h>            // Cliente NTP para data/hora
#include <WiFiUdp.h>
#include <Arduino.h>
#include <Preferences.h>

// ============ CONFIGURAÇÃO DO FIREBASE (SUBSTITUA PELOS SEUS DADOS) ============
#define API_KEY       "AIzaSyCORs2eW8qDTYe6SBA8o65nMoRVMAzhYXQ"           // Ex: "AIzaSyA..."
#define DATABASE_URL  "https://nivelbudo-default-rtdb.firebaseio.com/"  // Ex: "https://niveltec-default-rtdb.firebaseio.com"
#define USER_EMAIL    "teste@joaobatista.eng.br" // Email cadastrado no Firebase Auth
#define USER_PASSWORD "teste123456"             // Senha do usuário
// ===============================================================================

// Funções principais
void setupConexao();                          // Inicia WiFi, NTP e Firebase
bool isConnected();                           // Verifica se está conectado
unsigned long getTimestamp();                 // Timestamp Unix
String getFormattedTime();                    // Horário formatado HH:MM:SS
String getTimeString();                       // Data e hora completas
String getDeviceId();                         // ID único do dispositivo (MAC)

// Operações com Firebase
bool sendDataToFirebase(const String &path, FirebaseJson &json);
bool getConfigFromFirebase(int &intervaloMedicao, int &amostrasMedia,
                           String &modoEnvio, String &modoOperacao, bool &displayLigado);
void enviarConfigPadrao();
void enviarDados(float d1, float d2, float media, const String &modo);
void updateDeviceStatus(const String &status);
bool sendDeviceInfoToFirebase(const String &mac, const String &ip,
                              unsigned long firstBoot, unsigned long lastBoot,
                              unsigned long bootCount, const String &firmwareVersion,
                              const String &status);
#endif