#ifndef CONEXAO_H
#define CONEXAO_H

#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <Preferences.h>

// ============ CONFIGURAÇÃO DO FIREBASE ============
#define API_KEY       "AIzaSyCORs2eW8qDTYe6SBA8o65nMoRVMAzhYXQ"
#define DATABASE_URL  "https://nivelbudo-default-rtdb.firebaseio.com/"
#define USER_EMAIL    "teste@joaobatista.eng.br"
#define USER_PASSWORD "teste123456"
// ==================================================

// Variáveis globais do Firebase (definidas no .cpp)
extern FirebaseData fbdo;
extern FirebaseAuth auth;
extern FirebaseConfig config;

// Funções de Setup e Conexão
void setupConexao();                          
bool isConnected();                           
unsigned long getTimestamp();                 
String getFormattedTime();                    
String getTimeString();                       
String getDeviceId();                         

// Funções de Comunicação com Firebase
bool sendDataToFirebase(const String &path, FirebaseJson &json);

// Lê as configurações do Firebase (Incluindo o valorX)
bool getConfigFromFirebase(int &intervaloMedicao, int &amostrasMedia,
                           String &modoEnvio, String &modoOperacao, 
                           bool &displayLigado, float &valorX);

void enviarConfigPadrao();

// Envia os dados fundidos (y_fusao), as distâncias brutas e seus desvios
void enviarDados(float y_fusao, float sigma_y, float d1, float desv1, float d2, float desv2, unsigned long timestamp, const String &modo);

void updateDeviceStatus(const String &status);

// Envia o status de calibração (calibrado true/false) para o site
void enviarStatusCalibracao(bool calibrado);

bool sendDeviceInfoToFirebase(const String &mac, const String &ip,
                              unsigned long firstBoot, unsigned long lastBoot,
                              unsigned long bootCount, const String &firmwareVersion,
                              const String &status);
#endif