package su.xash.engine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Environment
import android.os.IBinder
import android.os.ParcelFileDescriptor
import androidx.core.app.NotificationCompat
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import java.io.BufferedReader
import java.io.File
import java.io.FileInputStream
import java.io.InputStreamReader

class DedicatedServerService : Service() {
	private val binder = LocalBinder()
	private val _status = MutableLiveData(false)
	val status: LiveData<Boolean> = _status

	private val _log = MutableLiveData<String>("")
	val log: LiveData<String> = _log

	private var serverGameDir: String = "valve"
	private var serverExtraArgs: String = "+map crossfire +port 27015 +maxplayers 16"

	private var serverThread: Thread? = null
	private var consoleThread: Thread? = null
	private var running = false

	inner class LocalBinder : android.os.Binder() {
		fun getService(): DedicatedServerService = this@DedicatedServerService
	}

	override fun onCreate() {
		super.onCreate()
		createNotificationChannel()
	}

	override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
		val command = intent?.getStringExtra("command") ?: return START_NOT_STICKY

		when (command) {
			"start" -> {
				serverGameDir = intent.getStringExtra("gameDir") ?: "valve"
				serverExtraArgs = intent.getStringExtra("extraArgs") ?: "+map crossfire +port 27015 +maxplayers 16"
				saveSettings()
				startServer()
			}
			"stop" -> stopServer()
		}
		return START_STICKY
	}

	override fun onBind(intent: Intent?): IBinder = binder

	private fun getBaseDir(): String {
		val prefs = getSharedPreferences("app_preferences", Context.MODE_PRIVATE)
		val useAndroidData = prefs.getBoolean("use_android_data", false)
		return if (useAndroidData) {
			getExternalFilesDir(null)?.absolutePath + "/xash"
		} else {
			prefs.getString("game_path", null)
				?: (Environment.getExternalStorageDirectory().absolutePath + "/xash")
		}
	}

	private fun startServer() {
		if (running) return
		running = true
		_status.postValue(true)
		_log.postValue("> Starting dedicated server...\n")

		val notification = createNotification()
		startForeground(NOTIFICATION_ID, notification)

		val extraArgs = serverExtraArgs.trim().split("\\s+".toRegex())
		val args = mutableListOf("xash", "-dedicated").apply {
			addAll(extraArgs)
		}

		_log.postValue("> Arguments: ${args.joinToString(" ")}\n")

		try {
			System.loadLibrary("xash_dedicated")

			val consoleFd = nativeInitConsole()
			if (consoleFd < 0) {
				_log.postValue("> Error: failed to create console pipe\n")
				return
			}

			startConsoleReader(consoleFd)

			val basedir = getBaseDir()
			val rodir = filesDir.absolutePath + "/gamelibs"
			val crashdir = File(filesDir, "crashes").absolutePath.also { File(it).mkdirs() }

			serverThread = Thread({
				try {
					nativeStartServer(args.toTypedArray(), serverGameDir, basedir, rodir, crashdir)
				} catch (e: Exception) {
					_log.postValue("> Error: ${e.message}\n")
				}
				_log.postValue("> Server process exited\n")
				_status.postValue(false)
				running = false
				stopForeground(STOP_FOREGROUND_REMOVE)
				stopSelf()
			}, "DedicatedServer")
			serverThread?.start()
		} catch (e: Exception) {
			_log.postValue("> Failed to start: ${e.message}\n")
			_status.postValue(false)
			running = false
			stopForeground(STOP_FOREGROUND_REMOVE)
			stopSelf()
		}
	}

	private fun startConsoleReader(fd: Int) {
		consoleThread = Thread({
			try {
				val pfd = ParcelFileDescriptor.adoptFd(fd)
				val fis = FileInputStream(pfd.fileDescriptor)
				val reader = BufferedReader(InputStreamReader(fis))
				while (running) {
					val line = reader.readLine() ?: break
					_log.postValue("$line\n")
				}
				reader.close()
			} catch (_: Exception) {
			}
		}, "ConsoleReader").apply { isDaemon = true; start() }
	}

	private fun stopServer() {
		_log.postValue("> Shutting down server...\n")
		running = false
		try {
			nativeStopServer()
		} catch (_: Exception) {
			android.os.Process.killProcess(android.os.Process.myPid())
		}
	}

	private fun saveSettings() {
		getSharedPreferences("server_settings", Context.MODE_PRIVATE).edit().apply {
			putString("gameDir", serverGameDir)
			putString("extraArgs", serverExtraArgs)
			apply()
		}
	}

	private fun createNotificationChannel() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			val channel = NotificationChannel(
				CHANNEL_ID, getString(R.string.channel_server),
				NotificationManager.IMPORTANCE_LOW
			).apply { setShowBadge(false) }
			val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
			manager.createNotificationChannel(channel)
		}
	}

	private fun createNotification(): Notification {
		val pendingIntent = PendingIntent.getActivity(
			this, 0,
			Intent(this, MainActivity::class.java).apply {
				flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
			},
			PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
		)
		val statusText = getString(
			R.string.server_notification_text,
			serverGameDir, "?", "?"
		)
		return NotificationCompat.Builder(this, CHANNEL_ID)
			.setContentTitle(getString(R.string.server_notification_title))
			.setContentText(statusText)
			.setSmallIcon(android.R.drawable.ic_menu_share)
			.setContentIntent(pendingIntent)
			.setOngoing(true)
			.setPriority(NotificationCompat.PRIORITY_LOW)
			.build()
	}

	companion object {
		private const val CHANNEL_ID = "server_status"
		private const val NOTIFICATION_ID = 1001
	}

	private external fun nativeInitConsole(): Int
	private external fun nativeStartServer(args: Array<String>, gamedir: String, basedir: String, rodir: String, crashdir: String)
	private external fun nativeStopServer()
}
