plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }

android { namespace = "com.fuckingbms.cast"; compileSdk = 35
    defaultConfig { applicationId = "com.fuckingbms.cast"; minSdk = 29; targetSdk = 35; versionCode = 2; versionName = "0.2.0" }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    testOptions { unitTests.isIncludeAndroidResources = true }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
