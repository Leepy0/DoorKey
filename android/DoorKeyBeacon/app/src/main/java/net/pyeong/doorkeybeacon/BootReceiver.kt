package net.pyeong.doorkeybeacon

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** 재부팅·앱 업데이트 후 자동 시작 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(c: Context, i: Intent) {
        when (i.action) {
            Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> {
                if (Prefs.enabled(c) && BeaconService.hasAdvertisePermission(c)) {
                    try {
                        BeaconService.start(c)
                    } catch (_: Exception) {
                        // 일부 기기에서 부팅 직후 포그라운드 서비스 시작이 거부될 수 있음 → 앱을 한 번 열면 재시작
                    }
                }
            }
        }
    }
}
