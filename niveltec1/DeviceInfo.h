#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

struct DeviceInfoData {
    String mac;
    String ip;
    unsigned long firstBoot;
    unsigned long lastBoot;
    unsigned long bootCount;
    String firmwareVersion;
    String status;
};

class DeviceInfo {
public:
    static void init();
    static void updateIP();
    static void updateStatus(const String& status);
    static String getMAC();
    static String getIP();
    static unsigned long getFirstBoot();
    static unsigned long getLastBoot();
    static unsigned long getBootCount();
    static String getFirmwareVersion();
    static String getStatus();
    static void print();
    static bool saveToFirebase();

private:
    static Preferences prefs;
    static DeviceInfoData data;
    static void load();
    static void save();
};

#endif