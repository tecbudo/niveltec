#ifndef CONEXAO_H
#define CONEXAO_H

#include <WiFiManager.h>
#include <FirebaseClient.h>       // Nova biblioteca Firebase (mobizt v1.x+)
#include <WiFiClientSecure.h>     // Necessário para a conexão SSL
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <Preferences.h>

// Inclui as credenciais de um arquivo externo (não versionado)
#include "secrets.h" 

// ============ FUNÇÕES PRINCIPAIS ============
void setupConexao();
bool isConnected();
unsigned long getTimestamp();
String getFormattedTime();
String getTimeString();
String getDeviceId();

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