// app.js — 智能饲养系统
App({
  onLaunch() {
    // 初始化本地日志
    const logs = wx.getStorageSync('logs') || []
    logs.unshift(Date.now())
    wx.setStorageSync('logs', logs)
  },

  globalData: {
    // 设备验证密码
    devicePassword: '123456',
    // 是否已通过验证
    isAuthenticated: false
  }
})
