#include "Calibracao.h"
#include <math.h>

Preferences Calibracao::prefs;

// Inicialização das variáveis estáticas
float Calibracao::ref_a = 0.0;
float Calibracao::ref_a_std = 0.0;
float Calibracao::ref_b = 0.0;
float Calibracao::ref_b_std = 0.0;
float Calibracao::ref_ap = 0.0;
float Calibracao::ref_ap_std = 0.0;
float Calibracao::ref_bp = 0.0;
float Calibracao::ref_bp_std = 0.0;
float Calibracao::x_known = 0.0;
bool Calibracao::calibrado = false;

void Calibracao::init() {
    prefs.begin("calibracao", false);
    load();
    prefs.end();
}

void Calibracao::setBase(float a, float a_std, float b, float b_std) {
    ref_a = a;
    ref_a_std = a_std;
    ref_b = b;
    ref_b_std = b_std;
    
    prefs.begin("calibracao", false);
    save();
    prefs.end();
}

void Calibracao::setNivelX(float ap, float ap_std, float bp, float bp_std, float x_conhecido) {
    ref_ap = ap;
    ref_ap_std = ap_std;
    ref_bp = bp;
    ref_bp_std = bp_std;
    x_known = x_conhecido;
    
    // Só consideramos calibrado se a base já tiver sido setada (a > 0)
    if (ref_a > 0 && ref_b > 0 && x_known > 0) {
        calibrado = true;
    }

    prefs.begin("calibracao", false);
    save();
    prefs.end();
}

void Calibracao::reset() {
    ref_a = ref_b = ref_ap = ref_bp = 0.0;
    ref_a_std = ref_b_std = ref_ap_std = ref_bp_std = 0.0;
    x_known = 0.0;
    calibrado = false;
    
    prefs.begin("calibracao", false);
    prefs.clear(); // Apaga tudo do namespace
    prefs.end();
}

bool Calibracao::isCalibrado() {
    return calibrado;
}

float Calibracao::getRefA() { return ref_a; }
float Calibracao::getRefAp() { return ref_ap; }
float Calibracao::getXConhecido() { return x_known; }

void Calibracao::save() {
    prefs.putFloat("a", ref_a);
    prefs.putFloat("a_std", ref_a_std);
    prefs.putFloat("b", ref_b);
    prefs.putFloat("b_std", ref_b_std);
    prefs.putFloat("ap", ref_ap);
    prefs.putFloat("ap_std", ref_ap_std);
    prefs.putFloat("bp", ref_bp);
    prefs.putFloat("bp_std", ref_bp_std);
    prefs.putFloat("x", x_known);
    prefs.putBool("calibrado", calibrado);
}

void Calibracao::load() {
    ref_a = prefs.getFloat("a", 0.0);
    ref_a_std = prefs.getFloat("a_std", 0.0);
    ref_b = prefs.getFloat("b", 0.0);
    ref_b_std = prefs.getFloat("b_std", 0.0);
    ref_ap = prefs.getFloat("ap", 0.0);
    ref_ap_std = prefs.getFloat("ap_std", 0.0);
    ref_bp = prefs.getFloat("bp", 0.0);
    ref_bp_std = prefs.getFloat("bp_std", 0.0);
    x_known = prefs.getFloat("x", 0.0);
    calibrado = prefs.getBool("calibrado", false);
}

ResultadoFusao Calibracao::calcularY(float a_med, float a_std, float b_med, float b_std, int n_amostras) {
    ResultadoFusao res;
    res.valido = false;
    res.y_fusao = 0;
    res.sigma_fusao = 0;
    res.y1 = 0;
    res.y2 = 0;

    // Segurança 1: Verifica se está calibrado e se os denominadores não são zero
    float denom1 = ref_a - ref_ap;
    float denom2 = ref_b - ref_bp;

    if (!calibrado || abs(denom1) < 0.01 || abs(denom2) < 0.01) {
        return res; // Retorna inválido se não estiver calibrado ou der divisão por zero
    }

    // 1. Cálculo das alturas individuais (y1 e y2)
    res.y1 = x_known * (ref_a - a_med) / denom1;
    res.y2 = x_known * (ref_b - b_med) / denom2;

    // 2. Erros padrão das médias (divisão pela raiz de N)
    float se_a  = ref_a_std / sqrt(n_amostras);
    float se_ap = ref_ap_std / sqrt(n_amostras);
    float se_a2 = a_std / sqrt(n_amostras);
    
    float se_b  = ref_b_std / sqrt(n_amostras);
    float se_bp = ref_bp_std / sqrt(n_amostras);
    float se_b2 = b_std / sqrt(n_amostras);

    // 3. Derivadas parciais para propagação de erro (Sensor 1)
    float dy_da  = x_known * (a_med - ref_ap) / (denom1 * denom1);
    float dy_dap = x_known * (ref_a - a_med) / (denom1 * denom1);
    float dy_da2 = -x_known / denom1;
    
    float var_y1 = (dy_da*dy_da * se_a*se_a) + (dy_dap*dy_dap * se_ap*se_ap) + (dy_da2*dy_da2 * se_a2*se_a2);

    // 4. Derivadas parciais para propagação de erro (Sensor 2)
    float dy2_db  = x_known * (b_med - ref_bp) / (denom2 * denom2);
    float dy2_dbp = x_known * (ref_b - b_med) / (denom2 * denom2);
    float dy2_db2 = -x_known / denom2;
    
    float var_y2 = (dy2_db*dy2_db * se_b*se_b) + (dy2_dbp*dy2_dbp * se_bp*se_bp) + (dy2_db2*dy2_db2 * se_b2*se_b2);

    // 5. Fusão de Kalman (Mínima Variância) com Proteção
    float var_total = var_y1 + var_y2;
    float y_fusao, sigma_fusao;

    if (var_total > 0.000001) {
        float K = var_y1 / var_total;
        y_fusao = res.y1 + K * (res.y2 - res.y1);
        sigma_fusao = sqrt((var_y1 * var_y2) / var_total);
    } else {
        // Fallback se ambas as variâncias forem zero
        y_fusao = (res.y1 + res.y2) / 2.0;
        sigma_fusao = 0.0;
    }

    res.y_fusao = y_fusao;
    res.sigma_fusao = sigma_fusao;
    res.valido = true;

    return res;
}
