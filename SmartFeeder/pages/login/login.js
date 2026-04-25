// pages/login/login.js — 安全验证页逻辑
const app = getApp()

Page({
  data: {
    password: '',        // 用户输入的密码
    showPassword: false, // 是否明文显示
    errorMsg: ''         // 错误提示文字
  },

  /* ========== 生命周期 ========== */
  onLoad() {
    // 如果已验证过，直接跳转仪表盘
    if (app.globalData.isAuthenticated) {
      this._navigateToDashboard()
    }
  },

  /* ========== 事件处理 ========== */

  // 密码输入
  onPasswordInput(e) {
    this.setData({
      password: e.detail.value,
      errorMsg: ''
    })
  },

  // 切换密码可见性
  toggleShowPassword() {
    this.setData({
      showPassword: !this.data.showPassword
    })
  },

  // 点击"进入系统"
  onLogin() {
    const { password } = this.data

    if (password.length !== 6) {
      this.setData({ errorMsg: '请输入 6 位数字密码' })
      return
    }

    if (password !== app.globalData.devicePassword) {
      this.setData({ errorMsg: '密码错误，请重新输入' })
      wx.vibrateShort({ type: 'heavy' })
      return
    }

    // 验证通过
    app.globalData.isAuthenticated = true
    wx.showToast({ title: '验证成功', icon: 'success', duration: 800 })

    setTimeout(() => {
      this._navigateToDashboard()
    }, 800)
  },

  /* ========== 内部方法 ========== */
  _navigateToDashboard() {
    wx.redirectTo({ url: '/pages/dashboard/dashboard' })
  }
})
