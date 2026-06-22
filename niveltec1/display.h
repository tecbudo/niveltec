#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <vector>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C  // ou 0x3D, dependendo do seu display
#define LOG_AREA_HEIGHT 32      // altura da área inferior (para logs)
#define MAIN_AREA_HEIGHT (SCREEN_HEIGHT - LOG_AREA_HEIGHT)

class Display {
public:
  Display();
  void begin();                 // Inicializa o display
  void clear();                 // Limpa toda a tela
  void update();                // Atualiza o buffer na tela
  
  void on();                    // Liga o display (via comando I2C)
  void off();                   // Desliga o display (economia de energia)
  
  void print(String mensagem);  // Exibe texto na área principal (fonte normal)
  void printLog(String mensagem); // Adiciona mensagem ao log (área inferior)
  void setStatus(int status);   // Altera ícone de status (canto superior direito)
  void showTime(String timestamp); // Exibe data/hora na área superior
  
  void exibirNivel(float valor, String unidade = "mm"); // Exibe valor principal em fonte grande
  void exibirSensores(float s1, float s2);             // Exibe valores dos sensores
  bool isDisponivel() const { return disponivel; }


private:
  Adafruit_SSD1306 display;
  int centroX, centroY;
  static const int MAX_LOG_LINES = 4;          // Número máximo de linhas de log
  std::vector<String> logMessages;             // Armazena mensagens de log
  bool disponivel;                             // verifica se o displey foi iniciado corretamente
  
  void desenharSimboloStatus(int status);      // Desenha ícone no canto superior direito
  void limparAreaLog();                        // Limpa apenas a área de log
  void limparAreaPrincipal();                  // Limpa apenas a área principal
};

extern Display display;  // instância global

#endif