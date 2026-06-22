#ifndef ARMAZENAMENTO_H
#define ARMAZENAMENTO_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

class Armazenamento {
public:
    static bool begin();                // Inicializa o LittleFS
    static bool salvarPendente(float d1, float d2, float media, unsigned long timestamp, const String& modo);
    static void enviarPendentes();      // Tenta reenviar todos os pendentes
    static bool temPendentes();         // Verifica se há dados pendentes
    static void limparPendentes();      // Remove todos os pendentes (opcional)
};

#endif