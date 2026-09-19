plugins { id("com.android.application") }

android {
    namespace = "net.hta.halotrial"
    compileSdk = 35
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "net.hta.halotrial"
        // AAudio is API 26. Vulkan 1.1 (required in the manifest) already put
        // the real floor well above 24, so this costs nothing we had.
        minSdk = 26
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

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets["main"].manifest.srcFile("src/main/AndroidManifest.xml")
}
