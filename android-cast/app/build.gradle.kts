plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }

android { namespace = "com.fuckingbms.cast"; compileSdk = 35
    defaultConfig { applicationId = "com.fuckingbms.cast"; minSdk = 29; targetSdk = 35; versionCode = 3; versionName = "0.3.0" }
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
