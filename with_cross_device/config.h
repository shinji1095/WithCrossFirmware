#pragma once

// ===== Streaming mode =====
// 0: RTP(JPEG) over UDP   [推奨: 既定]
// 1: RTSP(UDP) server
// 2: Legacy WebSocket (既存機能維持)
// 3: Legacy UDP raw (1 datagram = 1 JPEG) 既存互換
#ifndef STREAM_MODE
#define STREAM_MODE 0
#endif

// ===== Camera =====
#ifndef CAM_WIDTH
#define CAM_WIDTH   640
#endif
#ifndef CAM_HEIGHT
#define CAM_HEIGHT  480
#endif
#ifndef CAM_FPS
#define CAM_FPS     10
#endif
#ifndef CAM_JPEG_QUALITY   // esp32-camera の "小さいほど高画質"
#define CAM_JPEG_QUALITY  70   // 目安: 25~35 ≒ Baseline 70前後
#endif

// ===== RTP over UDP =====
#ifndef RTP_PAYLOAD_MTU
#define RTP_PAYLOAD_MTU 1400   // RTPヘッダを除くJPEG負荷の最大
#endif
#ifndef RTP_SSRC
#define RTP_SSRC 0x30555352     // 'R','S','U','0'など任意
#endif
#ifndef RTP_PT_JPEG
#define RTP_PT_JPEG 26          // 静的PT（JPEG）
#endif
#ifndef RTP_PORT
#define RTP_PORT 5540
#endif


// ===== RTSP =====
#ifndef RTSP_PORT
#define RTSP_PORT 8554
#endif
#ifndef RTSP_PATH
#define RTSP_PATH "/stream"
#endif

// ===== Auto stream (no BLE, no WS) ===================================
// 0: 既存どおり（BLEで接続情報を受け取り、WS経由で状態同期）
// 1: BLE/WS を使わず、起動したらすぐ PC に RTP/JPEG を送る
#ifndef AUTO_STREAM_NO_BLE
#define AUTO_STREAM_NO_BLE 0
#endif

#if AUTO_STREAM_NO_BLE
  // --- Wi-Fi (STA) ---
  #ifndef WIFI_STA_SSID
  #define WIFI_STA_SSID "aterm-cd7784-a"
  #endif
  #ifndef WIFI_STA_PSK
  #define WIFI_STA_PSK  "9d1d50c097fc7"
  #endif

  // --- RTP 送信先（PC） ---
  #ifndef RTP_DEST_IP
  #define RTP_DEST_IP   "192.168.10.101"   // ← 受信PCのIPに変更
  #endif
  #ifndef RTP_DEST_PORT
  #define RTP_DEST_PORT RTP_PORT          // 既定 5540
  #endif
#endif
