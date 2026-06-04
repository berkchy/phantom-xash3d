package su.xash.engine.ui.server

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.DedicatedServerService
import su.xash.engine.R
import su.xash.engine.databinding.FragmentDedicatedServerBinding
import su.xash.engine.model.Game
import java.io.File

class DedicatedServerFragment : Fragment() {
	private var _binding: FragmentDedicatedServerBinding? = null
	private val binding get() = _binding!!

	private var service: DedicatedServerService? = null
	private var bound = false
	private var availableGames: List<String> = emptyList()

	private val connection = object : ServiceConnection {
		override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
			service = (binder as DedicatedServerService.LocalBinder).getService()
			bound = true
			service?.let { observeServerState(it) }
		}

		override fun onServiceDisconnected(name: ComponentName?) {
			bound = false
		}
	}

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentDedicatedServerBinding.inflate(inflater, container, false)
		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		super.onViewCreated(view, savedInstanceState)

		loadSavedSettings()
		loadGameList()

		binding.startServerButton.setOnClickListener { startServer() }
		binding.stopServerButton.setOnClickListener { stopServer() }

		val intent = Intent(requireContext(), DedicatedServerService::class.java)
		requireContext().bindService(intent, connection, Context.BIND_AUTO_CREATE)
	}

	override fun onDestroyView() {
		super.onDestroyView()
		if (bound) {
			requireContext().unbindService(connection)
			bound = false
		}
		_binding = null
	}

	private fun loadSavedSettings() {
		val prefs = requireContext().getSharedPreferences("server_settings", Context.MODE_PRIVATE)
		binding.extraArgsInput.setText(
			prefs.getString("extraArgs", "+map crossfire +port 27015 +maxplayers 16")
		)
	}

	private fun loadGameList() {
		lifecycleScope.launch {
			val games = withContext(Dispatchers.IO) {
				val prefs = requireContext().getSharedPreferences("app_preferences", Context.MODE_PRIVATE)
				val useAndroidData = prefs.getBoolean("use_android_data", false)
				val rootPath = if (useAndroidData) {
					requireContext().getExternalFilesDir(null)?.absolutePath + "/xash"
				} else {
					prefs.getString("game_path", null)
						?: (android.os.Environment.getExternalStorageDirectory().absolutePath + "/xash")
				}
				File(rootPath).listFiles()
					?.filter { it.isDirectory && Game.checkIfGamedir(it) != null }
					?.map { it.name }
					?.sorted()
					?: emptyList()
			}
			availableGames = games
			val adapter = ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item, games)
			adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
			binding.gameSpinner.adapter = adapter

			val savedGame = requireContext().getSharedPreferences("server_settings", Context.MODE_PRIVATE)
				.getString("gameDir", null)
			if (savedGame != null) {
				val idx = games.indexOf(savedGame)
				if (idx >= 0) binding.gameSpinner.setSelection(idx)
			}

			if (games.isEmpty()) {
				binding.startServerButton.isEnabled = false
				binding.startServerButton.text = getString(R.string.no_games_found)
			}
		}
	}

	private fun startServer() {
		if (availableGames.isEmpty()) return

		val intent = Intent(requireContext(), DedicatedServerService::class.java).apply {
			putExtra("command", "start")
			putExtra("gameDir", binding.gameSpinner.selectedItem?.toString() ?: "valve")
			putExtra("extraArgs", binding.extraArgsInput.text.toString())
		}
		requireContext().startForegroundService(intent)

		binding.serverStatusLabel.text = getString(R.string.server_status_running)
		binding.serverStatusLabel.setTextColor(resources.getColor(R.color.hl_orange, null))
		binding.startServerButton.isEnabled = false
		binding.stopServerButton.isEnabled = true
	}

	private fun stopServer() {
		val intent = Intent(requireContext(), DedicatedServerService::class.java).apply {
			putExtra("command", "stop")
		}
		requireContext().startService(intent)

		binding.serverStatusLabel.text = getString(R.string.server_status_stopped)
		binding.startServerButton.isEnabled = true
		binding.stopServerButton.isEnabled = false
	}

	private fun observeServerState(service: DedicatedServerService) {
		service.status.observe(viewLifecycleOwner) { status ->
			binding.serverStatusLabel.text = if (status) {
				getString(R.string.server_status_running)
			} else {
				getString(R.string.server_status_stopped)
			}
			binding.serverStatusLabel.setTextColor(
				requireContext().getColor(
					if (status) R.color.hl_orange else android.R.color.darker_gray
				)
			)
			binding.startServerButton.isEnabled = !status && availableGames.isNotEmpty()
			binding.stopServerButton.isEnabled = status
		}

		service.log.observe(viewLifecycleOwner) { line ->
			appendConsole(line)
		}
	}

	private fun appendConsole(text: String) {
		binding.serverConsole.append(text)
		binding.consoleScroll.post { binding.consoleScroll.fullScroll(View.FOCUS_DOWN) }
	}
}
