#include "Sensores.h"
#include <Wire.h>

// Endereços I2C para os dois sensores (diferentes de 0x29 padrão)
#define LOX1_ADDRESS 0x30
#define LOX2_ADDRESS 0x31

Sensores::Sensores() {
    for (int i = 0; i < NUM_SENSORES; i++) {
        sensoresInicializados[i] = false;
        enderecos[i] = (i == 0) ? LOX1_ADDRESS : LOX2_ADDRESS;
        pinosXSHUT[i] = -1;
    }
}

void Sensores::iniciar(int xshut1, int xshut2) {
    pinosXSHUT[0] = xshut1;
    pinosXSHUT[1] = xshut2;

    pinMode(pinosXSHUT[0], OUTPUT);
    pinMode(pinosXSHUT[1], OUTPUT);

    // O I2C já deve ter sido iniciado no setup principal
    setID();

    Serial.println("Sensores VL53L0X inicializados.");
}

void Sensores::setID() {
    // 1. Coloca ambos os sensores em reset
    digitalWrite(pinosXSHUT[0], LOW);
    digitalWrite(pinosXSHUT[1], LOW);
    delay(0);

    // 2. Ativa o primeiro sensor e inicializa com endereço personalizado
    digitalWrite(pinosXSHUT[0], HIGH);
    digitalWrite(pinosXSHUT[1], LOW);
    delay(10);

    if (!lox[0].begin(LOX1_ADDRESS)) {
        Serial.println("Falha ao inicializar primeiro sensor VL53L0X");
        sensoresInicializados[0] = false;
    } else {
        sensoresInicializados[0] = true;
        Serial.println("Sensor 1 inicializado.");
    }

    // 3. Ativa o segundo sensor e inicializa com outro endereço
    digitalWrite(pinosXSHUT[1], HIGH);
    delay(10);

    if (!lox[1].begin(LOX2_ADDRESS)) {
        Serial.println("Falha ao inicializar segundo sensor VL53L0X");
        sensoresInicializados[1] = false;
    } else {
        sensoresInicializados[1] = true;
        Serial.println("Sensor 2 inicializado.");
    }
}

float Sensores::lerDistancia(int sensor) {
    if (sensor < 0 || sensor >= NUM_SENSORES) return -1;
    if (!sensoresInicializados[sensor]) return -1;

    VL53L0X_RangingMeasurementData_t measure;
    lox[sensor].rangingTest(&measure, false); // false = sem debug

    if (measure.RangeStatus != 4) { // 4 = out of range
        return measure.RangeMilliMeter;
    } else {
        return -1; // Fora do alcance
    }
}

bool Sensores::isSensorOk(int sensor) {
    return (sensor >= 0 && sensor < NUM_SENSORES && sensoresInicializados[sensor]);
}

void Sensores::calibrarSensores() {
    // Se necessário, implementar calibração futuramente
    Serial.println("Calibração não implementada para VL53L0X.");
}

void Sensores::printStatus() {
    for (int i = 0; i < NUM_SENSORES; i++) {
        Serial.print("Sensor ");
        Serial.print(i+1);
        Serial.print(": ");
        if (sensoresInicializados[i]) {
            float d = lerDistancia(i);
            if (d > 0) {
                Serial.print(d);
                Serial.println(" mm");
            } else {
                Serial.println("fora de alcance");
            }
        } else {
            Serial.println("não inicializado");
        }
    }
}

Sensores sensores;