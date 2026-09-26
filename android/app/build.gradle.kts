plugins { id("com.android.application") }

android {
    namespace = "net.hta.halotrial"
    compileSdk = 35
    ndkVersion = "28.0.13004108"

    defaultConfig {
        // MEGAMOD SHOWDOWN installs as its own app beside Open Halo's
        // (net.hta.halotrial). The Java package, and so every JNI name,
        // stays the shared engine's.
        applicationId = "net.megamod.showdown"
        // AAudio is API 26. Vulkan 1.1 (required in the manifest) already put
        // the real floor well above 24, so this costs nothing we had.
        minSdk = 26
        targetSdk = 35
        // Rises with every commit, so each sideloaded APK installs over the last.
        versionCode = (providers.gradleProperty("htaVersionCode").orNull ?: "1").toInt()
        versionName = "0.2-" + (providers.gradleProperty("htaVersionName").orNull ?: "dev")

        // Where SEND REPORT posts (the owner's sideload server, set by
        // publish_apk.sh for the personal build only). Empty: reports are
        // saved on the device and not sent.
        val reportUrl = providers.gradleProperty("htaReportUrl").orNull ?: ""
        buildConfigField("String", "REPORT_URL", "\"" + reportUrl.replace("\"", "") + "\"")
        buildConfigField("boolean", "PERSONAL", (providers.gradleProperty("htaAssetsDir").orNull != null).toString())

        // Modern ARM64 only, per the brief. No fat APK -- except the
        // emulator build (publish_apk.sh --emulator-apk), which adds x86_64
        // so an x86 Android emulator runs it at full speed.
        val abis = (providers.gradleProperty("htaAbis").orNull ?: "arm64-v8a").split(",").map { it.trim() }
        ndk { abiFilters += abis }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=none")
                cppFlags += ""
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.31.5"
        }
    }

    buildTypes {
        release { isMinifyEnabled = false }
        debug   { isJniDebuggable = true }
    }

    buildFeatures { buildConfig = true }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets["main"].manifest.srcFile("src/main/AndroidManifest.xml")

    // A PERSONAL build only: -PhtaAssetsDir points at a staging folder of the
    // owner's own Trial maps (never in git). Stored uncompressed so native
    // code can mmap them straight out of the APK. Never share such an APK.
    providers.gradleProperty("htaAssetsDir").orNull?.let { sourceSets["main"].assets.srcDir(it) }
    androidResources { noCompress += listOf("map", "oalmap", "oalasset") }
}
