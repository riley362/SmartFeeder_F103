// pages/dashboard/dashboard.js — 主控仪表盘 + 巴法云 MQTT 通信
const mqtt = require('../../utils/mqtt.min.js')
const MQTT_CFG = require('../../utils/mqtt-config.js')
const app = getApp()

/* 空气质量阈值（MQ-135 ADC 原始值，0~4095） */
const AIR_LEVELS = [
  { max: 1500, label: '优', level: 'good' },
  { max: 2500, label: '良', level: 'moderate' },
  { max: 4096, label: '差', level: 'bad' }
]

/* AI 宠物识别码 → 中文名 (参考 main.c:241-244) */
const PET_MAP = { 0: '猫', 1: '狗', 2: '兔子' }

Page({
  data: {
    /* ======= 1. 状态纵览（由硬件上报驱动） ======= */
    lightIntensity: 0,          // 光照强度 (Lux) ← data.lux
    airQuality: '--',           // 空气质量文字   ← data.air 阈值映射
    airQualityLevel: 'good',    // good | moderate | bad
    waterLevel: 0,              // 饮水余量 (%)    ← data.water (0/1 → 85/15)
    waterLow: false,            // 饮水不足告警
    foodLevel: 0,               // 食物余量 (%)    ← data.food  (0/1 → 80/12)
    foodLow: false,             // 食物不足告警
    petType: '',                // AI 识别结果      ← data.pet 映射

    /* ======= 2. 控制中心（本地状态，配合下行指令） ======= */
    isAutoMode: true,           // true=自动，false=手动
    lightOn: false,             // 补光灯开关
    feedAmount: '50',           // 投喂量 (g)

    /* ======= 3. 连接状态 ======= */
    netConnected: false,        // 小程序 ↔ 云端
    deviceOnline: false,        // 硬件是否在线（基于上报心跳）

    /* ======= 4. 定时喂食 ======= */
    schedules: [],           // [{id, time:'08:00', amount:30, enabled:true, firedToday:false}]
    newSchedTime: '08:00',
    newSchedAmount: '30',

    /* ======= 5. 系统信息 ======= */
    systemTime: '--:--:--',
    feedCountToday: 0
  },

  /* 非响应式实例字段 */
  _clockTimer: null,
  _offlineTimer: null,
  _lastMsgTs: 0,
  _mqttClient: null,

  /* ========== 生命周期 ========== */
  onLoad() {
    this.setData({ schedules: this._loadSchedules() })
    this._startClock()
    this._startOfflineWatcher()
    this._connectMQTT()
  },

  onUnload() {
    this._stopClock()
    this._stopOfflineWatcher()
    this._disconnectMQTT()
  },

  onHide() {
    // 退到后台时保持时钟运行，但减少网络压力：此处不断开，若需节流可在此 end()
  },

  /* ================================================
   * MQTT 连接 / 订阅 / 消息处理
   * ================================================ */

  _connectMQTT() {
    if (this._mqttClient) return

    const that = this
    wx.showLoading({ title: '连接云端...', mask: false })

    // 巴法云鉴权规则：clientId 必须严格等于 32 位 UID，多一个字符都会被拒绝
    // 同一 UID 允许在 TCP(9501)/WS(9504) 各建一条连接，不会互相踢掉
    const clientId = MQTT_CFG.UID

    this._mqttClient = mqtt.connect(MQTT_CFG.BROKER_URL, {
      clientId,
      keepalive: MQTT_CFG.KEEPALIVE,
      clean: true,
      protocolVersion: 4,
      connectTimeout: MQTT_CFG.CONNECT_TIMEOUT,
      reconnectPeriod: 3000
    })

    this._mqttClient.on('connect', () => {
      wx.hideLoading()
      wx.showToast({ title: '云端已连接', icon: 'success', duration: 900 })
      that.setData({ netConnected: true })
      console.log('[MQTT] connected as', clientId)

      // 订阅硬件上报主题
      that._mqttClient.subscribe(MQTT_CFG.TOPIC_UP, { qos: 0 }, (err, granted) => {
        if (err) {
          console.error('[MQTT] subscribe error:', err)
        } else {
          console.log('[MQTT] subscribed:', granted)
        }
      })
    })

    this._mqttClient.on('message', (topic, payload) => {
      if (topic !== MQTT_CFG.TOPIC_UP) return
      const msg = payload.toString()
      console.log('[MQTT RX]', topic, msg)

      try {
        const data = JSON.parse(msg)

        // --- 硬件开机同步请求: 回复时间 + 同步模式到 UI ---
        if (data.cmd === 'get_time') {
          that._replyTimeSync()
          // 硬件上报了当前模式, 同步到 UI
          if (data.mode === 'auto' || data.mode === 'manual') {
            that.setData({ isAutoMode: data.mode === 'auto' })
            console.log('[SYNC] device mode →', data.mode)
          }
          return
        }

        that._applyTelemetry(data)
      } catch (e) {
        console.error('[MQTT] JSON parse failed:', msg, e)
      }
    })

    this._mqttClient.on('reconnect', () => {
      console.log('[MQTT] reconnecting...')
    })

    this._mqttClient.on('error', (err) => {
      wx.hideLoading()
      console.error('[MQTT] error:', err)
    })

    this._mqttClient.on('offline', () => {
      console.warn('[MQTT] offline')
      that.setData({ netConnected: false, deviceOnline: false })
    })

    this._mqttClient.on('close', () => {
      console.warn('[MQTT] close')
      that.setData({ netConnected: false, deviceOnline: false })
    })
  },

  _disconnectMQTT() {
    if (this._mqttClient) {
      try { this._mqttClient.end(true) } catch (e) {}
      this._mqttClient = null
    }
  },

  /**
   * 发布控制指令到硬件下行主题
   * @param {object} obj — 会被 JSON.stringify 后发送
   * @return {boolean}  是否成功送入发送队列
   */
  _publishCommand(obj) {
    if (!this._mqttClient || !this.data.netConnected) {
      wx.showToast({ title: '云端未连接', icon: 'none' })
      return false
    }
    const json = JSON.stringify(obj)
    this._mqttClient.publish(MQTT_CFG.TOPIC_DOWN, json, { qos: 0 }, (err) => {
      if (err) console.error('[MQTT TX] publish error:', err)
    })
    console.log('[MQTT TX]', MQTT_CFG.TOPIC_DOWN, json)
    return true
  },

  /**
   * 把硬件 JSON 遥测映射到 UI 状态
   * 硬件格式参考 main.c:308-322：
   *   {"air":1800,"lux":254.2,"water":0,"food":0,"pet":255}
   */
  _applyTelemetry(data) {
    const patch = {}

    // --- air (MQ-135 ADC, 0~4095) → 空气质量三档 ---
    const air = Number(data.air)
    if (!isNaN(air)) {
      const hit = AIR_LEVELS.find(l => air < l.max) || AIR_LEVELS[AIR_LEVELS.length - 1]
      patch.airQuality = hit.label
      patch.airQualityLevel = hit.level
    }

    // --- lux (float) → 光照强度 ---
    const lux = Number(data.lux)
    if (!isNaN(lux)) {
      patch.lightIntensity = Math.round(lux)
    }

    // --- water / food (0=OK, 1=LOW) ---
    if (data.water !== undefined) {
      const low = Number(data.water) === 1
      patch.waterLow = low
      patch.waterLevel = low ? 15 : 85
    }
    if (data.food !== undefined) {
      const low = Number(data.food) === 1
      patch.foodLow = low
      patch.foodLevel = low ? 12 : 80
    }

    // --- pet (AI ID, 255 = 未识别) ---
    if (data.pet !== undefined) {
      patch.petType = PET_MAP[Number(data.pet)] || ''
    }

    // --- light (0=关, 1=开)：物理按钮或云端指令都会改变此值，前端跟随显示 ---
    if (data.light !== undefined) {
      patch.lightOn = Number(data.light) === 1
    }

    // --- feed_cnt (硬件侧累计的今日投喂次数) ---
    if (data.feed_cnt !== undefined) {
      const cnt = Number(data.feed_cnt)
      if (!isNaN(cnt)) patch.feedCountToday = cnt
    }

    // --- 活跃心跳 ---
    patch.deviceOnline = true
    this._lastMsgTs = Date.now()

    this.setData(patch)
  },

  _startOfflineWatcher() {
    this._offlineTimer = setInterval(() => {
      if (!this._lastMsgTs) return
      if (Date.now() - this._lastMsgTs > MQTT_CFG.DEVICE_OFFLINE_MS) {
        if (this.data.deviceOnline) {
          this.setData({ deviceOnline: false })
        }
      }
    }, 3000)
  },

  _stopOfflineWatcher() {
    if (this._offlineTimer) {
      clearInterval(this._offlineTimer)
      this._offlineTimer = null
    }
  },

  /* ================================================
   * UI 交互 → 下行控制指令
   * ================================================ */

  /**
   * 切换运行模式（自动 / 手动）
   * 下行 JSON: {"cmd":"mode","value":"auto"|"manual"}
   */
  onSwitchMode(e) {
    const mode = e.currentTarget.dataset.mode
    const isAuto = mode === 'auto'
    if (isAuto === this.data.isAutoMode) return

    this.setData({
      isAutoMode: isAuto,
      lightOn: isAuto ? false : this.data.lightOn
    })
    this._publishCommand({ cmd: 'mode', value: isAuto ? 'auto' : 'manual' })
  },

  /**
   * 补光灯开关
   * 下行 JSON: {"cmd":"light","value":"on"|"off"}
   */
  onToggleLight() {
    if (this.data.isAutoMode) return
    const next = !this.data.lightOn
    this.setData({ lightOn: next })
    this._publishCommand({ cmd: 'light', value: next ? 'on' : 'off' })
  },

  /**
   * 投喂量输入（本地）
   */
  onFeedAmountInput(e) {
    this.setData({ feedAmount: e.detail.value })
  },

  /**
   * 立即投喂
   * 下行 JSON: {"cmd":"feed","amount":<number>}
   */
  onFeedNow() {
    if (this.data.isAutoMode) return
    const amount = Number(this.data.feedAmount)
    if (!amount || amount <= 0) {
      wx.showToast({ title: '请输入有效克数', icon: 'none' })
      return
    }

    wx.showModal({
      title: '确认投喂',
      content: `即将投喂 ${amount}g，是否继续？`,
      success: (res) => {
        if (!res.confirm) return
        const ok = this._publishCommand({ cmd: 'feed', amount })
        if (!ok) return
        this.setData({ feedCountToday: this.data.feedCountToday + 1 })
        wx.showToast({ title: '投喂指令已发送', icon: 'success' })
      }
    })
  },

  /* ================================================
   * 系统时钟
   * ================================================ */

  _startClock() {
    this._updateTime()
    this._clockTimer = setInterval(() => {
      this._updateTime()
      this._checkSchedules()
    }, 1000)
  },

  _stopClock() {
    if (this._clockTimer) {
      clearInterval(this._clockTimer)
      this._clockTimer = null
    }
  },

  _updateTime() {
    const now = new Date()
    const pad = (n) => String(n).padStart(2, '0')
    const timeStr =
      pad(now.getHours()) + ':' +
      pad(now.getMinutes()) + ':' +
      pad(now.getSeconds())
    this.setData({ systemTime: timeStr })
  },

  /* ================================================
   * 定时喂食
   * ================================================ */

  onSchedTimePick(e) {
    this.setData({ newSchedTime: e.detail.value })
  },

  onSchedAmountInput(e) {
    this.setData({ newSchedAmount: e.detail.value })
  },

  onAddSchedule() {
    const time = this.data.newSchedTime
    const amount = Number(this.data.newSchedAmount)
    if (!time || !amount || amount <= 0) {
      wx.showToast({ title: '请填写有效时间和克数', icon: 'none' })
      return
    }
    const dup = this.data.schedules.find(s => s.time === time)
    if (dup) {
      wx.showToast({ title: '该时间已存在计划', icon: 'none' })
      return
    }
    const list = [...this.data.schedules, {
      id: Date.now(),
      time,
      amount,
      enabled: true,
      firedToday: false
    }]
    list.sort((a, b) => a.time.localeCompare(b.time))
    this.setData({ schedules: list })
    this._saveSchedules(list)
    wx.showToast({ title: '已添加', icon: 'success', duration: 800 })
  },

  onToggleSchedule(e) {
    const idx = e.currentTarget.dataset.idx
    const key = `schedules[${idx}].enabled`
    const cur = this.data.schedules[idx].enabled
    this.setData({ [key]: !cur })
    this._saveSchedules(this.data.schedules)
  },

  onRemoveSchedule(e) {
    const idx = e.currentTarget.dataset.idx
    const list = this.data.schedules.filter((_, i) => i !== idx)
    this.setData({ schedules: list })
    this._saveSchedules(list)
  },

  _checkSchedules() {
    const now = new Date()
    const hm = String(now.getHours()).padStart(2, '0') + ':' +
               String(now.getMinutes()).padStart(2, '0')
    const sec = now.getSeconds()
    let changed = false

    // 每天 00:00:00 重置 firedToday
    if (hm === '00:00' && sec === 0) {
      this.data.schedules.forEach(s => { s.firedToday = false })
      changed = true
    }

    this.data.schedules.forEach((s, i) => {
      if (s.enabled && !s.firedToday && s.time === hm && sec === 0) {
        const ok = this._publishCommand({ cmd: 'feed', amount: s.amount })
        if (ok) {
          s.firedToday = true
          changed = true
          console.log('[SCHED] fired:', s.time, s.amount + 'g')
          wx.showToast({ title: s.time + ' 定时投喂 ' + s.amount + 'g', icon: 'none', duration: 2000 })
        }
      }
    })

    if (changed) {
      this.setData({ schedules: this.data.schedules })
      this._saveSchedules(this.data.schedules)
    }
  },

  _saveSchedules(list) {
    wx.setStorageSync('feed_schedules', list.map(s => ({
      time: s.time, amount: s.amount, enabled: s.enabled
    })))
  },

  _loadSchedules() {
    const saved = wx.getStorageSync('feed_schedules') || []
    return saved.map(s => ({
      id: Date.now() + Math.random(),
      time: s.time,
      amount: s.amount,
      enabled: s.enabled !== false,
      firedToday: false
    }))
  },

  /* ================================================
   * 时间同步: 硬件开机请求 → 小程序回复当前时间
   * 下行 JSON: {"cmd":"time","h":17,"m":30,"s":0,"Y":2026,"M":4,"D":24}
   * ================================================ */
  _replyTimeSync() {
    const now = new Date()
    const timeObj = {
      cmd: 'time',
      h: now.getHours(),
      m: now.getMinutes(),
      s: now.getSeconds(),
      Y: now.getFullYear(),
      M: now.getMonth() + 1,
      D: now.getDate()
    }
    const ok = this._publishCommand(timeObj)
    if (ok) {
      console.log('[SYNC] replied time:', JSON.stringify(timeObj))
      wx.showToast({ title: '已同步时间', icon: 'success', duration: 800 })
    }
  }
})
