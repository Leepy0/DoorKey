plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "net.pyeong.doorkeybeacon"
    compileSdk = 35

    defaultConfig {
        applicationId = "net.pyeong.doorkeybeacon"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // 개인용: 디버그 키로 서명해서 바로 설치
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

// 외부 라이브러리 없음 (플랫폼 API만 사용)
dependencies {
}
