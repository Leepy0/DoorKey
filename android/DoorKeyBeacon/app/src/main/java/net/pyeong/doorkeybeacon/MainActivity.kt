package net.pyeong.doorkeybeacon

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView

/**
 * 설정 화면. 외부 라이브러리 없이 플랫폼 View만 사용한다
 * (Gradle 의존성 다운로드가 추가로 필요 없도록).
 */
class MainActivity : Activity() {

    private val handler = Handler(Looper.getMainLooper())
    private lateinit var statusText: TextView
    private lateinit var permText: TextView
    private lateinit var sw: Switch

    private val ticker = object : Runnable {
        override fun run() {
            refresh()
            handler.postDelayed(this, 1000)
        }
    }

    private fun Int.dp() = (this * resources.displayMetrics.density).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(20.dp(), 16.dp(), 20.dp(), 32.dp())
        }
        val scroll = ScrollView(this).apply {
            fitsSystemWindows = true  // 상태바/내비게이션바 영역 피하기 (Android 15 edge-to-edge)
            addView(root)
        }
        setContentView(scroll)

        root.addView(TextView(this).apply {
            text = "DoorKey 비컨"
            textSize = 24f
            setTypeface(typeface, Typeface.BOLD)
        })
        root.addView(note("현관 ESP32가 이 폰을 알아보도록 BLE 광고를 계속 내보냅니다."))

        statusText = TextView(this).apply {
            textSize = 16f
            setPadding(0, 16.dp(), 0, 4.dp())
        }
        root.addView(statusText)

        sw = Switch(this).apply {
            text = "비컨 켜기"
            textSize = 18f
            setPadding(0, 8.dp(), 0, 8.dp())
            isChecked = Prefs.enabled(this@MainActivity)
            setOnCheckedChangeListener { _, on -> onToggle(on) }
        }
        root.addView(sw)

        // ---- 광고 간격
        root.addView(header("광고 간격"))
        root.addView(radio(
            listOf("저전력 (1초)", "균형 (250ms, 권장)", "빠름 (100ms)"),
            Prefs.mode(this)
        ) { Prefs.setMode(this, it); restartIfOn() })
        root.addView(note("간격이 짧을수록 문 앞에서 빨리 잡히지만 배터리를 조금 더 씁니다."))

        // ---- 송신 세기
        root.addView(header("송신 세기"))
        root.addView(radio(listOf("낮음", "중간 (권장)", "높음"), Prefs.tx(this)) {
            Prefs.setTx(this, it); restartIfOn()
        })
        root.addView(note("ESP 웹 UI에서 엘리베이터 앞 RSSI가 충분히 나오면 중간이면 됩니다."))

        // ---- 권한/설정
        root.addView(header("권한"))
        permText = TextView(this).apply { textSize = 14f }
        root.addView(permText)
        root.addView(button("권한 허용") { requestPerms() })
        root.addView(button("배터리 최적화 제외") { requestBatteryExemption() })
        root.addView(button("블루투스 설정 열기 (DoorKey 페어링)") {
            startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
        })

        root.addView(header("처음 설정"))
        root.addView(note(
            "1. ESP 웹 UI › 기기 › 새 폰 등록 › 등록 시작\n" +
            "2. 위 '블루투스 설정 열기' › 스캔 › DoorKey 등록\n" +
            "3. 등록 완료 후 블루투스 목록에서 DoorKey 삭제해도 됨\n" +
            "4. 이 앱에서 비컨 켜기\n" +
            "5. ESP 웹 UI › 기기 › '갤럭시 앱 비컨'이 '해석됨'인지 확인\n" +
            "6. 삼성 설정 › 배터리 › 백그라운드 사용 제한 › '절전 예외 앱'에 이 앱 추가 " +
            "(One UI 버전에 따라 메뉴 이름이 조금 다를 수 있음)"
        ))
    }

    override fun onResume() {
        super.onResume()
        // 켜져 있어야 하는데 서비스가 죽어 있으면 다시 시작
        if (Prefs.enabled(this) && BeaconService.hasAdvertisePermission(this) && !BeaconService.running) {
            BeaconService.start(this)
        }
        handler.post(ticker)
    }

    override fun onPause() {
        handler.removeCallbacks(ticker)
        super.onPause()
    }

    // ---------------------------------------------------------------- 동작

    private fun onToggle(on: Boolean) {
        Prefs.setEnabled(this, on)
        if (on) {
            if (!BeaconService.hasAdvertisePermission(this)) {
                requestPerms()
                return
            }
            BeaconService.start(this)
        } else {
            BeaconService.stop(this)
        }
        refresh()
    }

    private fun restartIfOn() {
        if (Prefs.enabled(this) && BeaconService.running) BeaconService.restart(this)
    }

    private fun requestPerms() {
        val perms = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) perms += Manifest.permission.BLUETOOTH_ADVERTISE
        if (Build.VERSION.SDK_INT >= 33) perms += Manifest.permission.POST_NOTIFICATIONS
        val missing = perms.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) {
            refresh()
            return
        }
        requestPermissions(missing.toTypedArray(), 1)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (Prefs.enabled(this) && BeaconService.hasAdvertisePermission(this)) BeaconService.start(this)
        refresh()
    }

    @SuppressLint("BatteryLife")
    private fun requestBatteryExemption() {
        val pm = getSystemService(PowerManager::class.java)
        if (pm != null && pm.isIgnoringBatteryOptimizations(packageName)) {
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        } else {
            startActivity(
                Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName"))
            )
        }
    }

    private fun refresh() {
        val on = Prefs.enabled(this)
        if (sw.isChecked != on) sw.isChecked = on
        val st = if (BeaconService.running) BeaconService.status else "정지"
        val up = if (BeaconService.advertising && BeaconService.since > 0) {
            val m = (System.currentTimeMillis() - BeaconService.since) / 60000
            " · ${if (m < 60) "${m}분" else "${m / 60}시간 ${m % 60}분"}째"
        } else ""
        statusText.text = "상태: $st$up"

        val adv = BeaconService.hasAdvertisePermission(this)
        val noti = Build.VERSION.SDK_INT < 33 ||
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
        val batt = getSystemService(PowerManager::class.java)?.isIgnoringBatteryOptimizations(packageName) == true
        permText.text = "${mark(adv)} 블루투스 광고\n${mark(noti)} 알림\n${mark(batt)} 배터리 최적화 제외"
    }

    private fun mark(ok: Boolean) = if (ok) "✔" else "✘"

    // ---------------------------------------------------------------- 뷰 조각

    private fun header(t: String) = TextView(this).apply {
        text = t
        textSize = 16f
        setTypeface(typeface, Typeface.BOLD)
        setPadding(0, 20.dp(), 0, 4.dp())
    }

    private fun note(t: String) = TextView(this).apply {
        text = t
        textSize = 13f
        alpha = 0.7f
        setPadding(0, 2.dp(), 0, 2.dp())
    }

    private fun button(t: String, onClick: () -> Unit) = Button(this).apply {
        text = t
        isAllCaps = false
        layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = 4.dp() }
        setOnClickListener { onClick() }
    }

    private fun radio(options: List<String>, selected: Int, onSelect: (Int) -> Unit): RadioGroup {
        val g = RadioGroup(this).apply { orientation = RadioGroup.VERTICAL }
        options.forEachIndexed { i, label ->
            g.addView(RadioButton(this).apply {
                id = View.generateViewId()
                text = label
                tag = i
                isChecked = i == selected
            })
        }
        g.setOnCheckedChangeListener { group, checkedId ->
            val idx = group.findViewById<RadioButton>(checkedId)?.tag as? Int ?: return@setOnCheckedChangeListener
            onSelect(idx)
        }
        return g
    }
}
