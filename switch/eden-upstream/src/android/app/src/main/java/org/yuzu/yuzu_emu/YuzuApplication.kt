// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import org.yuzu.yuzu_emu.features.input.NativeInput
import java.io.File
import java.io.FileOutputStream
import java.security.KeyStore
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager
import android.content.res.Configuration
import android.os.LocaleList
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.utils.DirectoryInitialization
import org.yuzu.yuzu_emu.utils.DocumentsTree
import org.yuzu.yuzu_emu.utils.GpuDriverHelper
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.utils.PowerStateUpdater
import org.yuzu.yuzu_emu.utils.ControllerNavigationGlobalHook
import java.util.Locale

fun Context.getPublicFilesDir(): File = getExternalFilesDir(null) ?: filesDir

class YuzuApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        initialize(this)
    }

    companion object {
        @Volatile
        private var initialized = false

        var documentsTree: DocumentsTree? = null
        lateinit var application: Application
            private set

        val appContext: Context
            get() = application.applicationContext

        /**
         * Initializes Eden's process-wide runtime without requiring Eden to own the Android
         * [Application]. Dualscreen Launcher calls this lazily before the first Switch operation;
         * the standalone Eden application calls the same entry point from [onCreate].
         */
        @Synchronized
        fun initialize(hostApplication: Application, userDirectory: File? = null) {
            if (initialized) return
            application = hostApplication
            documentsTree = DocumentsTree()
            DirectoryInitialization.configureUserDirectory(userDirectory)
            DirectoryInitialization.start()

            // Keep Eden's tested initialization order intact. The native runtime is loaded only
            // when Switch functionality is first requested, never during a PC-only launch.
            GpuDriverHelper.initializeFreedrenoConfigEarly()
            NativeLibrary.playTimeManagerInit()
            GpuDriverHelper.initializeDriverParameters()
            NativeInput.reloadInputDevices()
            NativeLibrary.logDeviceInfo()
            PowerStateUpdater.start()
            Log.logDeviceInfo()
            ControllerNavigationGlobalHook.install(hostApplication)
            createNotificationChannels(hostApplication)
            initialized = true
        }

        fun isInitialized(): Boolean = initialized

        private fun createNotificationChannels(context: Context) {
            val name: CharSequence = context.getString(R.string.app_notification_channel_name)
            val description = context.getString(R.string.app_notification_channel_description)
            val foregroundService = NotificationChannel(
                context.getString(R.string.app_notification_channel_id),
                name,
                NotificationManager.IMPORTANCE_DEFAULT
            )
            foregroundService.description = description
            foregroundService.setSound(null, null)
            foregroundService.vibrationPattern = null

            val noticeChannel = NotificationChannel(
                context.getString(R.string.notice_notification_channel_id),
                context.getString(R.string.notice_notification_channel_name),
                NotificationManager.IMPORTANCE_HIGH
            )
            noticeChannel.description =
                context.getString(R.string.notice_notification_channel_description)
            noticeChannel.setSound(null, null)

            val notificationManager = context.getSystemService(NotificationManager::class.java)
            notificationManager.createNotificationChannel(noticeChannel)
            notificationManager.createNotificationChannel(foregroundService)
        }

        private val LANGUAGE_CODES = arrayOf(
            "system", "en", "es", "fr", "de", "it", "pt", "pt-BR", "ru", "ja", "ko",
            "zh-CN", "zh-TW", "pl", "cs", "nb", "hu", "uk", "vi", "id", "ar", "ckb", "fa", "he", "sr"
        )

        fun applyLanguage(context: Context): Context {
            // Android can restore EmulationActivity directly after process death, before the
            // host application has had a chance to initialize Eden. Use the host locale for
            // attachBaseContext; EmulationActivity initializes the runtime at the start of
            // onCreate and all subsequent resource lookups use the configured language.
            if (!initialized) return context
            val languageIndex = IntSetting.APP_LANGUAGE.getInt()
            val langCode = if (languageIndex in LANGUAGE_CODES.indices) {
                LANGUAGE_CODES[languageIndex]
            } else {
                "system"
            }

            if (langCode == "system") {
                return context
            }

            val locale = when {
                langCode.contains("-") -> {
                    val parts = langCode.split("-")
                    Locale.Builder().setLanguage(parts[0]).setRegion(parts[1]).build()
                }
                else -> Locale.Builder().setLanguage(langCode).build()
            }

            Locale.setDefault(locale)

            val config = Configuration(context.resources.configuration)
            config.setLocales(LocaleList(locale))

            return context.createConfigurationContext(config)
        }
    }
}
