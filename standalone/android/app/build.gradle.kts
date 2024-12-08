plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.jetbrains.kotlin.android)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.ksp)
    alias(libs.plugins.ktfmt)
}

val versionNameValue: String by lazy {
    val value = project.findProperty("versionName")?.toString() ?: "10.8.1.0000"
    println("versionName: $value")
    value
}

val versionCodeValue: Int by lazy {
    val parts = versionNameValue.split(".").map { it.toIntOrNull() ?: error("Invalid version part in $versionNameValue") }
    require(parts.size == 4) { "Version name must have exactly 4 parts: MAJ.MIN.REV.BUILD" }
    val (maj, min, rev, build) = parts
    require(maj in 0..99 && min in 0..99 && rev in 0..9 && build in 0..9999) {
        "Version parts out of range: MAJ[0-99], MIN[0-99], REV[0-9], BUILD[0-9999]"
    }
    val value = (maj * 10000000) + (min * 100000) + (rev * 10000) + build
    println("versionCode: $value")
    value
}

tasks {
    val copyLibs by registering(Copy::class) {
       val buildDir = layout.buildDirectory.asFile.get().path
       val destinationDir = file("$projectDir/src/main/jniLibs/arm64-v8a")
       destinationDir.listFiles()?.forEach { it.deleteRecursively() }
       from("$buildDir/../../../../build/android") { include("**.so") }
       into(destinationDir)
    }

    val copyAssets by registering(Copy::class) {
       val buildDir = layout.buildDirectory.asFile.get().path
       val destinationDir = file("$projectDir/src/main/assets")
       destinationDir.listFiles()?.forEach { it.deleteRecursively() }
       from("$buildDir/../../../../build/android") {
          include("assets/**")
          include("flexdmd/**")
          include("scripts/**")
       } 
       into(destinationDir)
    }

    preBuild {
        dependsOn(copyLibs, copyAssets)
    }
}

android {
    namespace = "org.vpinball.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.vpinball.vpinball_bgfx"
        minSdk = 30
        targetSdk = 35
        versionCode = versionCodeValue
        versionName = versionNameValue

        vectorDrawables { useSupportLibrary = true }

        ndk { abiFilters += "arm64-v8a" }
    }

    signingConfigs {
        create("release") {
            storeFile = file("$projectDir/vpinball.jks")
            storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
            keyAlias = System.getenv("ANDROID_KEY_ALIAS")
            keyPassword = System.getenv("ANDROID_KEY_PASSWORD")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("release")
        }
    }

    sourceSets { named("main") { jniLibs.srcDir("src/main/jniLibs") } }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
    packaging { resources { excludes += "/META-INF/{AL2.0,LGPL2.1}" } }
}

ktfmt {
    kotlinLangStyle()
    maxWidth.set(150)
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.material3)
    val composeBom = platform(libs.androidx.compose.bom)
    implementation(composeBom)
    implementation(libs.ui.tooling.preview)
    debugImplementation(libs.ui.tooling)

    implementation(libs.androidx.room.runtime)
    annotationProcessor(libs.androidx.room.compiler)
    ksp(libs.androidx.room.compiler)
    implementation(libs.androidx.room.ktx)
    implementation(libs.androidx.browser)
    implementation(libs.haze)

    implementation(libs.kotlinx.datetime)
    implementation(libs.koin.compose)
    implementation(libs.koin.compose.navigation)
}
