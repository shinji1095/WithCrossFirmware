#pragma once
#include <Arduino.h>

#include "WsAgent.h"
#include "esp_camera.h"
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"
#include "UdpAgent.h"

class UdpAgent;

class CameraStreamer {
public:
    bool begin();      
    void stream(WsAgent& ws);   
    void stream(class UdpAgent& udp);              
private:
    uint32_t _interval = 100;
    uint32_t _tLast = 0;
    uint32_t _nextOkAfter  = 0;
    void initCameraConfig(camera_config_t&);
};

