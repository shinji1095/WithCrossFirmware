#include "BleAgent.h"
#include <Arduino.h>
#include "NetDebug.h"

/* --- Server connection state ----------------------------------------- */
void ServerCB::onConnect(NimBLEServer*, NimBLEConnInfo&)  { _a->_connected = true;  LOGI("BLE","connected"); }
void ServerCB::onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) { _a->_connected = false; LOGW("BLE","disconnected"); }

/* --- Characteristic write -> parse creds ----------------------------- */
void CharCB::onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) {
    String v = c->getValue().c_str();                     // ssid|psk|ip|port
    LOGD("BLE","Raw: %s", v.c_str());                     /// LOG

    int p1=v.indexOf('|'), p2=v.indexOf('|',p1+1), p3=v.indexOf('|',p2+1);
    if (p1<0 || p2<0 || p3<0) { Serial.println("[BLE] bad format"); return; }

    BleAgent::Creds cr;
    cr.ssid = v.substring(0, p1);
    cr.psk  = v.substring(p1+1, p2);
    cr.ip   = v.substring(p2+1, p3);
    cr.port = v.substring(p3+1).toInt();

    LOGI("BLE","SSID=%s PSK=%s IP=%s PORT=%u",
         cr.ssid.c_str(), cr.psk.c_str(), cr.ip.c_str(), cr.port);    /// LOG

    _a->_cb(cr);                       
}

/* --- Advertising control --------------------------------------------- */
bool BleAgent::begin(CredsCallback cb) {
    _cb = cb;
    NimBLEDevice::init("WithCrossDevice");          
    NimBLEDevice::setDeviceName("WithCrossDevice"); 
    NimBLEDevice::setMTU(128);
    NimBLEServer* svr = NimBLEDevice::createServer();
    svr->setCallbacks(new ServerCB(this));

    auto* svc = svr->createService("0000180A-0000-1000-8000-00805f9b34fb");
    auto* ch  = svc->createCharacteristic(
        "00002A26-0000-1000-8000-00805f9b34fb",
        NIMBLE_PROPERTY::WRITE);
    ch->setCallbacks(new CharCB(this));

    svc->start();
    NimBLEAdvertising* adv = svr->getAdvertising();

    // --- 広告データ（Advertise Data）：サービスUUID＋（可能なら）名前 ---
    NimBLEAdvertisementData advData;
    advData.addServiceUUID(svc->getUUID());
    advData.setName("WithCrossDevice");   // 端末によってはここだけで十分に表示される
    adv->setAdvertisementData(advData);

    // --- スキャン応答データ（Scan Response Data）：名前を確実に載せる ---
    NimBLEAdvertisementData scanData;
    scanData.setName("WithCrossDevice");
    adv->setScanResponseData(scanData);

    adv->start();
    _t0 = millis();
    LOGI("BLE","Advertising (10-s timeout loop)");                     /// LOG
    return true;
}

void BleAgent::restartAdvertise() {
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::getAdvertising()->start();
    _t0 = millis();
    LOGI("BLE","Adv restart");                                         /// LOG
}

void BleAgent::loop() {
    if (!_connected && millis() - _t0 >= 10'000) restartAdvertise();
}

/* ---- stop(): advertising & NimBLE stack 停止 ----------------------- */
void BleAgent::stop() {
    NimBLEDevice::getAdvertising()->stop();
    // NimBLEDevice::deinit(false);
    LOGI("BLE","Stopped");                                             /// LOG
}
