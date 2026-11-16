#ifndef ARMAZENAMENTO_H
#define ARMAZENAMENTO_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

class Armazenamento {
public:
    static bool begin();                // Inicializa o LittleFS
    
    // ATUALIZADO: Agora salva todos os parâmetros (y, sigma, s1, desv1, s2, desv2)
    static bool salvarPendente(float y_fusao, float sigma_y, float d1, float desv1, float d2, float desv2, unsigned long timestamp, const String& modo);
    
    static void enviarPendentes();      // Tenta reenviar todos os pendentes
    static bool temPendentes();         // Verifica se há dados pendentes
    static void limparPendentes();      // Remove todos os pendentes (opcional)
};

#endif