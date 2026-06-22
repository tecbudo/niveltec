#ifndef SENSORES_H
#define SENSORES_H

#include <Arduino.h>
#include "Adafruit_VL53L0X.h"

#define NUM_SENSORES 2

class Sensores {
public:
    Sensores();
    void iniciar(int xshut1, int xshut2);
    float lerDistancia(int sensor); // 0 = sensor1, 1 = sensor2
    bool isSensorOk(int sensor);
    void calibrarSensores(); // reservado para futuras melhorias
    void printStatus();

private:
    Adafruit_VL53L0X lox[2];
    uint8_t enderecos[2];
    bool sensoresInicializados[2];
    int pinosXSHUT[2];
    
    void setID();
};

extern Sensores sensores;

#endif