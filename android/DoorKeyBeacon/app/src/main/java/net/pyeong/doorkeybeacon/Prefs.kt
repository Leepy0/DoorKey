package net.pyeong.doorkeybeacon

import android.bluetooth.le.AdvertisingSetParameters
import android.content.Context
import android.os.ParcelUuid

object Prefs {
    // ESP 펌웨어 config.h 의 APP_BEACON_UUID 와 같아야 한다 (진단 표시용)
    val SERVICE_UUID: ParcelUuid = ParcelUuid.fromString("6d0f5a1e-3c4b-4e8a-9b2f-7a1c0d4e5f60")

    private fun sp(c: Context) = c.getSharedPreferences("beacon", Context.MODE_PRIVATE)

    fun enabled(c: Context) = sp(c).getBoolean("enabled", false)
    fun setEnabled(c: Context, v: Boolean) = sp(c).edit().putBoolean("enabled", v).apply()

    /** 0 = 저전력(1초), 1 = 균형(250ms), 2 = 빠름(100ms) */
    fun mode(c: Context) = sp(c).getInt("mode", 1)
    fun setMode(c: Context, v: Int) = sp(c).edit().putInt("mode", v).apply()

    /** 0 = 낮음, 1 = 중간, 2 = 높음 */
    fun tx(c: Context) = sp(c).getInt("tx", 1)
    fun setTx(c: Context, v: Int) = sp(c).edit().putInt("tx", v).apply()

    fun interval(c: Context) = when (mode(c)) {
        0 -> AdvertisingSetParameters.INTERVAL_HIGH    // 약 1초
        2 -> AdvertisingSetParameters.INTERVAL_LOW     // 약 100ms
        else -> AdvertisingSetParameters.INTERVAL_MEDIUM // 약 250ms
    }

    fun txPower(c: Context) = when (tx(c)) {
        0 -> AdvertisingSetParameters.TX_POWER_LOW
        2 -> AdvertisingSetParameters.TX_POWER_HIGH
        else -> AdvertisingSetParameters.TX_POWER_MEDIUM
    }
}
