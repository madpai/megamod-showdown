plugins { id("com.android.application") }

android {
    namespace = "net.hta.halotrial"
    compileSdk = 35
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "net.hta.halotrial"
        minSdk = 24              // Vulkan + AHardwareBuffer era; S24+ is far above
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-poc"

        // Modern ARM64 only, per the brief. No fat APK.
        ndk { abiFilters += listOf("arm64-v8a") }

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

    // No Java/Kotlin source at all: pure NativeActivity.
    sourceSets["main"].manifest.srcFile("src/main/AndroidManifest.xml")
}
