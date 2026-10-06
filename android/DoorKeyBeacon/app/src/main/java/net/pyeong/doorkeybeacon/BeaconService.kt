package net.pyeong.doorkeybeacon

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertisingSet
import android.bluetooth.le.AdvertisingSetCallback
import android.bluetooth.le.AdvertisingSetParameters
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper

/**
 * 폰이 BLE 광고를 계속 내보내게 하는 포그라운드 서비스.
 *
 * 광고 주소는 안드로이드가 폰의 IRK로 만든 RPA(약 15분마다 바뀜)라서,
 * ESP는 등록 때 받은 IRK로 "이 폰"임을 확인한다. 앱에 비밀값은 없다.
 */
class BeaconService : Service() {

    companion object {
        private const val CH_ID = "beacon"
        private const val NOTI_ID = 1
        private const val ACTION_RESTART = "restart"
        private const val HEALTH_MS = 10 * 60 * 1000L
        private const val RETRY_MS = 30 * 1000L

        @Volatile var running = false; private set
        @Volatile var advertising = false; private set
        @Volatile var status = "정지"; private set
        @Volatile var since = 0L; private set

        fun start(c: Context) {
            c.startForegroundService(Intent(c, BeaconService::class.java))
        }

        fun restart(c: Context) {
            c.startForegroundService(Intent(c, BeaconService::class.java).setAction(ACTION_RESTART))
        }

        fun stop(c: Context) {
            c.stopService(Intent(c, BeaconService::class.java))
        }

        fun hasAdvertisePermission(c: Context): Boolean =
            Build.VERSION.SDK_INT < 31 ||
                c.checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) == PackageManager.PERMISSION_GRANTED
    }

    private val handler = Handler(Looper.getMainLooper())
    private var advertiser: BluetoothLeAdvertiser? = null
    private var started = false  // startAdvertisingSet 호출 후 stop 전까지

    private val callback = object : AdvertisingSetCallback() {
        override fun onAdvertisingSetStarted(set: AdvertisingSet?, txPower: Int, st: Int) {
            if (st == ADVERTISE_SUCCESS) {
                advertising = true
                since = System.currentTimeMillis()
                setStatus("광고 중 (송신 ${txPower}dBm)")
            } else {
                advertising = false
                started = false
                setStatus("광고 시작 실패 (코드 $st) — 30초 뒤 재시도")
                handler.postDelayed(retry, RETRY_MS)
            }
        }

        override fun onAdvertisingSetStopped(set: AdvertisingSet?) {
            advertising = false
        }
    }

    private val retry = Runnable { startAdvertising() }

    // 주기 점검: 광고가 조용히 멈춰 있으면 다시 시작
    private val health = object : Runnable {
        override fun run() {
            if (!advertising) {
                stopAdvertising()
                startAdvertising()
            }
            handler.postDelayed(this, HEALTH_MS)
        }
    }

    private val btReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            when (i.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1)) {
                BluetoothAdapter.STATE_ON -> startAdvertising()
                BluetoothAdapter.STATE_TURNING_OFF, BluetoothAdapter.STATE_OFF -> {
                    advertising = false
                    started = false
                    setStatus("블루투스 꺼짐")
                }
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
        val filter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(btReceiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            registerReceiver(btReceiver, filter)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        try {
            startForeground(NOTI_ID, buildNotification(status), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } catch (e: Exception) {
            // 블루투스 광고 권한이 회수된 경우 등 → 조용히 종료 (앱을 열어 다시 허용)
            status = "시작 실패: ${e.message}"
            stopSelf()
            return START_NOT_STICKY
        }
        running = true
        if (intent?.action == ACTION_RESTART || !started) {
            stopAdvertising()
            startAdvertising()
        }
        handler.removeCallbacks(health)
        handler.postDelayed(health, HEALTH_MS)
        return START_STICKY
    }

    @SuppressLint("MissingPermission")
    private fun startAdvertising() {
        handler.removeCallbacks(retry)
        if (started) return
        val adapter = getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter == null) {
            setStatus("블루투스 없음")
            return
        }
        if (!adapter.isEnabled) {
            setStatus("블루투스 꺼짐")
            return
        }
        if (!hasAdvertisePermission(this)) {
            setStatus("권한 없음 — 앱에서 권한을 허용하세요")
            return
        }
        val adv = adapter.bluetoothLeAdvertiser
        if (adv == null) {
            setStatus("BLE 광고 미지원")
            return
        }
        advertiser = adv

        // 레거시 + 연결 가능 광고: 안드로이드가 IRK 기반 RPA를 쓰도록 (비연결 광고는 기기에 따라 NRPA를 쓸 수 있음)
        val params = AdvertisingSetParameters.Builder()
            .setLegacyMode(true)
            .setConnectable(true)
            .setScannable(true)
            .setInterval(Prefs.interval(this))
            .setTxPowerLevel(Prefs.txPower(this))
            .build()
        val data = AdvertiseData.Builder()
            .addServiceUuid(Prefs.SERVICE_UUID)
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .build()
        try {
            adv.startAdvertisingSet(params, data, null, null, null, callback)
            started = true
            setStatus("광고 시작 중")
        } catch (e: Exception) {
            setStatus("광고 시작 오류: ${e.message}")
            handler.postDelayed(retry, RETRY_MS)
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopAdvertising() {
        if (started) {
            try {
                advertiser?.stopAdvertisingSet(callback)
            } catch (_: Exception) {
            }
        }
        started = false
        advertising = false
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        stopAdvertising()
        try {
            unregisterReceiver(btReceiver)
        } catch (_: Exception) {
        }
        running = false
        status = "정지"
        super.onDestroy()
    }

    // ---------------------------------------------------------------- 알림

    private fun setStatus(s: String) {
        status = s
        getSystemService(NotificationManager::class.java)?.notify(NOTI_ID, buildNotification(s))
    }

    private fun createChannel() {
        val ch = NotificationChannel(CH_ID, "비컨 동작", NotificationManager.IMPORTANCE_LOW).apply {
            setShowBadge(false)
            description = "DoorKey 비컨이 동작 중임을 표시합니다"
        }
        getSystemService(NotificationManager::class.java)?.createNotificationChannel(ch)
    }

    private fun buildNotification(text: String): Notification {
        val pi = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return Notification.Builder(this, CH_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("DoorKey 비컨")
            .setContentText(text)
            .setOngoing(true)
            .setContentIntent(pi)
            .build()
    }
}
