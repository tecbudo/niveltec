#ifndef CALIBRACAO_H
#define CALIBRACAO_H

#include <Arduino.h>
#include <Preferences.h>

// Estrutura para retornar o resultado da fusão de forma organizada
struct ResultadoFusao {
    float y_fusao;       // Altura calculada final (mm)
    float sigma_fusao;   // Incerteza da altura final (mm)
    float y1;            // Altura calculada pelo Sensor 1 (apenas para debug)
    float y2;            // Altura calculada pelo Sensor 2 (apenas para debug)
    bool valido;         // Retorna false se a matemática falhar
};

class Calibracao {
public:
    // Inicializa a classe e carrega dados salvos na memória
    static void init();

    // Salva os dados coletados na base (a e b) e no nível conhecido X (a' e b')
    static void setBase(float a, float a_std, float b, float b_std);
    static void setNivelX(float ap, float ap_std, float bp, float bp_std, float x_conhecido);

    // Limpa a calibração da memória
    static void reset();

    // Verifica se o dispositivo já foi calibrado
    static bool isCalibrado();

    // Retorna os valores atuais (útil para enviar ao Firebase)
    static float getRefA();
    static float getRefAp();
    static float getXConhecido();

    // Realiza a Fusão de Kalman (A matemática pesada)
    // Recebe as médias e desvios padrões das leituras atuais (a'' e b'')
    static ResultadoFusao calcularY(float a_med, float a_std, float b_med, float b_std, int n_amostras);

private:
    static Preferences prefs;
    
    // Variáveis de referência salvas
    static float ref_a, ref_a_std;
    static float ref_b, ref_b_std;
    static float ref_ap, ref_ap_std;
    static float ref_bp, ref_bp_std;
    static float x_known;
    static bool calibrado;

    // Funções auxiliares para salvar/carregar da memória
    static void save();
    static void load();
};

#endif
