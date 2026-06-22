#include "DeviceInfo.h"
#include "Conexao.h"   // para getTimestamp(), isConnected(), sendDeviceInfoToFirebase()

Preferences DeviceInfo::prefs;
DeviceInfoData DeviceInfo::data;

void DeviceInfo::init() {
    prefs.begin("device_info", false);
    load();
    
    data.mac = WiFi.macAddress();
    data.ip = WiFi.localIP().toString();
    
    if (data.bootCount == 0) {
        data.firstBoot = getTimestamp();
    }
    data.lastBoot = getTimestamp();
    data.bootCount++;
    data.firmwareVersion = "1.0.0";
    data.status = "inicializando";
    
    save();
    prefs.end();
    
    Serial.println("DeviceInfo inicializado:");
    print();
}

void DeviceInfo::load() {
    data.mac = prefs.getString("mac", "");
    data.ip = prefs.getString("ip", "");
    data.firstBoot = prefs.getULong("firstBoot", 0);
    data.lastBoot = prefs.getULong("lastBoot", 0);
    data.bootCount = prefs.getULong("bootCount", 0);
    data.firmwareVersion = prefs.getString("firmwareVersion", "1.0.0");
    data.status = prefs.getString("status", "inicializando");
}

void DeviceInfo::save() {
    prefs.putString("mac", data.mac);
    prefs.putString("ip", data.ip);
    prefs.putULong("firstBoot", data.firstBoot);
    prefs.putULong("lastBoot", data.lastBoot);
    prefs.putULong("bootCount", data.bootCount);
    prefs.putString("firmwareVersion", data.firmwareVersion);
    prefs.putString("status", data.status);
}

void DeviceInfo::updateIP() {
    data.ip = WiFi.localIP().toString();
    prefs.begin("device_info", false);
    save();
    prefs.end();
}

void DeviceInfo::updateStatus(const String& status) {
    data.status = status;
    prefs.begin("device_info", false);
    save();
    prefs.end();
}

String DeviceInfo::getMAC() { return data.mac; }
String DeviceInfo::getIP() { return data.ip; }
unsigned long DeviceInfo::getFirstBoot() { return data.firstBoot; }
unsigned long DeviceInfo::getLastBoot() { return data.lastBoot; }
unsigned long DeviceInfo::getBootCount() { return data.bootCount; }
String DeviceInfo::getFirmwareVersion() { return data.firmwareVersion; }
String DeviceInfo::getStatus() { return data.status; }

void DeviceInfo::print() {
    Serial.println("=== Device Info ===");
    Serial.println("MAC: " + data.mac);
    Serial.println("IP: " + data.ip);
    Serial.println("First Boot: " + String(data.firstBoot));
    Serial.println("Last Boot: " + String(data.lastBoot));
    Serial.println("Boot Count: " + String(data.bootCount));
    Serial.println("Firmware: " + data.firmwareVersion);
    Serial.println("Status: " + data.status);
    Serial.println("===================");
}

bool DeviceInfo::saveToFirebase() {
    if (!isConnected()) return false;
    return sendDeviceInfoToFirebase(
        data.mac,
        data.ip,
        data.firstBoot,
        data.lastBoot,
        data.bootCount,
        data.firmwareVersion,
        data.status
    );
}