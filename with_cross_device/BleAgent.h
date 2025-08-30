#pragma once
#include <NimBLEDevice.h>
#include <functional>

class BleAgent;      // 本体

class ServerCB : public NimBLEServerCallbacks {
  public: explicit ServerCB(BleAgent* a):_a(a){}
    void onConnect   (NimBLEServer*, NimBLEConnInfo&)            override;
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int)       override;
  private: BleAgent* _a;
};

class CharCB : public NimBLECharacteristicCallbacks {
  public: explicit CharCB(BleAgent* a):_a(a){}
    void onWrite(NimBLECharacteristic*, NimBLEConnInfo&)         override;
  private: BleAgent* _a;
};

class BleAgent {
public:
    struct Creds { String ssid, psk, ip; uint16_t port; };

    using CredsCallback = std::function<void(const Creds&)>;

    bool  begin(CredsCallback cb);     // start advertising (10-s loop)
    void  loop();                      // call every loop()
    bool  isConnected() const { return _connected; }
    void  stop();

private:
    CredsCallback _cb;
    uint32_t      _t0        = 0;
    bool          _connected = false;
    void restartAdvertise();

    friend class ServerCB;
    friend class CharCB;
};
