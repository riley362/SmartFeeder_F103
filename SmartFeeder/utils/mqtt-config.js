// utils/mqtt-config.js
// ============================================================
// 巴法云 MQTT 接入参数 —— 必须与硬件端 esp8266.h 中的定义完全一致
// 硬件端 (STM32 + ESP8266) 走 TCP: bemfa.com:9501
// 小程序端走 WebSocket Secure: wxs://bemfa.com:9504/wss
// ------------------------------------------------------------
// 注意：发布/部署时需在微信公众平台"开发设置 → 服务器域名"中
//       把下列域名加入白名单：
//   socket 合法域名:  wss://bemfa.com
//   request 合法域名: https://api.bemfa.com   (若使用状态查询 API)
// 开发阶段可在微信开发者工具右上角"详情 → 本地设置" 勾选
//   "不校验合法域名、web-view、TLS 版本以及 HTTPS 证书"
// ============================================================

module.exports = {
  // 巴法云 WebSocket MQTT 接入点
  BROKER_URL: 'wxs://bemfa.com:9504/wss',

  // 账号私钥 (UID) —— 同时也是 MQTT clientId 前缀
  // 与硬件端 BEMFA_UID 保持一致
  UID: 'YOUR_BEMFA_UID',

  // 设备上报主题：硬件 publish → 小程序 subscribe
  TOPIC_UP: 'feeder003',

  // 控制下行主题：小程序 publish → 硬件 subscribe
  TOPIC_DOWN: 'feederctrl003',

  // MQTT keepalive / 超时参数
  KEEPALIVE: 60,
  CONNECT_TIMEOUT: 5000,

  // 超过此时长未收到设备上报，则判定设备离线 (ms)
  // 硬件端固定 5s 上报一次，这里留 3 个周期容差
  DEVICE_OFFLINE_MS: 15000,

  // 巴法云状态查询 API（可选，用于冷启动时快速判断设备是否在线）
  STATUS_API: 'https://api.bemfa.com/api/device/v1/',
}
