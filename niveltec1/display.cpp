#include "display.h"

Display::Display() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {
  centroX = SCREEN_WIDTH / 2;
  centroY = MAIN_AREA_HEIGHT / 2;
}
/*
void Display::begin() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("Display não encontrado! Operando sem display.");
        Serial.flush();  // Força o envio do buffer (opcional, mas útil em debug)
        delay(100); 
        disponivel = false;  // variável de controle
        return;
    }
  disponivel = true;
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}*/
void Display::begin() {
  // VERIFICAÇÃO FÍSICA REAL: tenta comunicar com o dispositivo no endereço esperado
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);  // Modo comando
  Wire.write(0xAE);  // Comando: Display OFF (comando válido para SSD1306)
  byte error = Wire.endTransmission();
  
  // Se não respondeu ou respondeu com erro, display não está presente
  if (error != 0) {
    Serial.println("Display não encontrado! Operando sem display.");
    Serial.flush();
    delay(50);  // Garante que a mensagem seja transmitida antes de continuar
    
    disponivel = false;
    return;
  }
  
  // Se passou na verificação física, tenta inicializar com a biblioteca
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("Display não encontrado (falha na biblioteca)! Operando sem display.");
    Serial.flush();
    delay(50);
    disponivel = false;
    return;
  }
  
  // Teste final: tenta desenhar e atualizar
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("OK");
  display.display();
  delay(10);
  
  // Se chegou aqui, display está funcional
  disponivel = true;
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  Serial.println("Display inicializado com sucesso!");
}

void Display::clear() {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.clearDisplay();
  logMessages.clear();
}

void Display::update() {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.display();
}

void Display::on() {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.ssd1306_command(SSD1306_DISPLAYON); // Liga o display
}

void Display::off() {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.ssd1306_command(SSD1306_DISPLAYOFF); // Desliga o display
}

void Display::print(String mensagem) {
  // Limpa a área principal
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  limparAreaPrincipal();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(mensagem);
  update();
}

void Display::printLog(String mensagem) {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  // Adiciona a nova mensagem no início (mais recente)
  logMessages.insert(logMessages.begin(), mensagem);
  if (logMessages.size() > MAX_LOG_LINES) {
    logMessages.pop_back();
  }
  
  // Limpa a área de log
  limparAreaLog();
  display.setTextSize(1);
  for (int i = 0; i < logMessages.size(); i++) {
    display.setCursor(0, MAIN_AREA_HEIGHT + i * 8);
    display.println(logMessages[i]);
  }
  update();
}

void Display::setStatus(int status) {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  // Apaga o ícone anterior (canto superior direito)
  display.fillRect(SCREEN_WIDTH - 16, 0, 16, 16, SSD1306_BLACK);
  desenharSimboloStatus(status);
  update();
}

void Display::showTime(String timestamp) {
  // Limpa a área da hora (primeira linha da área principal)
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.fillRect(0, 0, SCREEN_WIDTH - 16, 8, SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(timestamp);
  update();
}

void Display::exibirNivel(float valor, String unidade) {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  // Limpa a área principal (exceto as duas primeiras linhas que podem ter hora e status)
  limparAreaPrincipal();
  
  // Exibe o valor em fonte grande
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  String texto = String(valor, 0) + unidade;
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  int y = (MAIN_AREA_HEIGHT - h) / 2;
  display.setCursor(x, y);
  display.println(texto);
  update();
}

void Display::exibirSensores(float s1, float s2) {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  // Exibe os valores dos sensores na parte inferior da área principal (linha extra)
  display.fillRect(0, MAIN_AREA_HEIGHT - 16, SCREEN_WIDTH, 16, SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(0, MAIN_AREA_HEIGHT - 16);
  display.print("S1: ");
  display.print(s1, 0);
  display.print(" mm  S2: ");
  display.print(s2, 0);
  display.print(" mm");
  update();
}

// --- Funções auxiliares privadas ---

void Display::limparAreaPrincipal() {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.fillRect(0, 0, SCREEN_WIDTH, MAIN_AREA_HEIGHT, SSD1306_BLACK);
}

void Display::limparAreaLog() {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  display.fillRect(0, MAIN_AREA_HEIGHT, SCREEN_WIDTH, LOG_AREA_HEIGHT, SSD1306_BLACK);
}

void Display::desenharSimboloStatus(int status) {
  if (!disponivel) return; // Se não tem tela, não faz nada e não trava
  int x = SCREEN_WIDTH - 14;
  int y = 2;
  switch(status) {
    case 1: // Desconectado (X)
      display.drawLine(x, y, x+12, y+12, SSD1306_WHITE);
      display.drawLine(x+12, y, x, y+12, SSD1306_WHITE);
      break;
    case 2: // Conectado (✓)
      display.drawLine(x, y+6, x+4, y+10, SSD1306_WHITE);
      display.drawLine(x+4, y+10, x+12, y+2, SSD1306_WHITE);
      break;
    case 3: // App conectado (✓✓) - duplo
      display.drawLine(x, y+4, x+4, y+8, SSD1306_WHITE);
      display.drawLine(x+4, y+8, x+12, y, SSD1306_WHITE);
      display.drawLine(x, y+10, x+4, y+14, SSD1306_WHITE);
      display.drawLine(x+4, y+14, x+12, y+6, SSD1306_WHITE);
      break;
    case 4: // Manutenção (chave)
      display.drawLine(x, y+2, x+4, y+6, SSD1306_WHITE);
      display.drawLine(x+4, y+6, x+8, y+2, SSD1306_WHITE);
      display.drawLine(x+8, y+2, x+12, y+6, SSD1306_WHITE);
      display.drawLine(x+12, y+6, x+8, y+10, SSD1306_WHITE);
      display.drawLine(x+8, y+10, x+4, y+6, SSD1306_WHITE);
      break;
  }
}

Display display; // instância global