import java.util.Properties

plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }

val companionBuildProperties = Properties().apply {
    rootProject.file("local.properties").takeIf { it.isFile }?.inputStream()?.use { load(it) }
}
val defaultTiandituKey = companionBuildProperties.getProperty("companion.tiandituKey", "").trim()
require(defaultTiandituKey.isEmpty() || defaultTiandituKey.matches(Regex("[a-fA-F0-9]{32}"))) {
    "companion.tiandituKey must be a 32-character hexadecimal key"
}
val defaultAmapJsKey = companionBuildProperties.getProperty("companion.amapJsKey", "").trim()
// shortcut: APK credentials can be extracted; use a serviceHost proxy before public distribution.
val defaultAmapSecurityCode = companionBuildProperties.getProperty("companion.amapSecurityJsCode", "").trim()
require(listOf(defaultAmapJsKey, defaultAmapSecurityCode).all { it.isEmpty() || it.matches(Regex("[a-fA-F0-9]{32}")) }) {
    "Companion Amap JS credentials must be 32-character hexadecimal values"
}

android { namespace = "com.fuckingbms.cast"; compileSdk = 35
    defaultConfig { applicationId = "com.fuckingbms.cast"; minSdk = 29; targetSdk = 35; versionCode = 3; versionName = "0.3.0"; testInstrumentationRunner = "com.fuckingbms.cast.CompanionInstrumentedChecks" }
    buildFeatures { buildConfig = true }
    defaultConfig { buildConfigField("String", "DEFAULT_TIANDITU_KEY", "\"$defaultTiandituKey\"") }
    defaultConfig {
        buildConfigField("String", "DEFAULT_AMAP_JS_KEY", "\"$defaultAmapJsKey\"")
        buildConfigField("String", "DEFAULT_AMAP_SECURITY_JS_CODE", "\"$defaultAmapSecurityCode\"")
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    testOptions { unitTests.isIncludeAndroidResources = true }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}

val dashboardAssets by tasks.registering(Sync::class) {
    from("../../vercel/dist") { exclude("apk/**", "firmware/**") }
    into(layout.buildDirectory.dir("generated/dashboard-assets/dashboard"))
    doFirst { check(file("../../vercel/dist/index.html").isFile) { "Build vercel frontend first: npm --prefix ../vercel run build" } }
}
android.sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/dashboard-assets"))
tasks.named("preBuild").configure { dependsOn(dashboardAssets) }
