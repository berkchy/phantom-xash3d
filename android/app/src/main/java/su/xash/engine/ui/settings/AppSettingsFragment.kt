package su.xash.engine.ui.settings

import android.content.Context
import android.content.SharedPreferences
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import su.xash.engine.R
import su.xash.engine.databinding.FragmentAppSettingsBinding
import java.io.File

class AppSettingsFragment : Fragment() {
	private var _binding: FragmentAppSettingsBinding? = null
	private val binding get() = _binding!!

	private val prefs: SharedPreferences by lazy {
		requireContext().getSharedPreferences(APP_PREFS, Context.MODE_PRIVATE)
	}

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentAppSettingsBinding.inflate(inflater, container, false)
		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		super.onViewCreated(view, savedInstanceState)

		bindStorage()
		bindDisplay()
		bindRuntime()
		refreshAll()
	}

	private fun bindStorage() = with(binding) {
		useAndroidDataSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_USE_ANDROID_DATA, checked).apply()
			gamePathInput.isEnabled = !checked
			refreshGamePathSummary()
		}

		gamePathInput.doAfterTextChanged {
			prefs.edit().putString(KEY_GAME_PATH, it?.toString().orEmpty()).apply()
			refreshGamePathSummary()
		}
	}

	private fun bindDisplay() = with(binding) {
		renderResolutionButton.setOnClickListener { showRenderResolutionDialog() }

		displayModeGroup.addOnButtonCheckedListener { _, checkedId, isChecked ->
			if (!isChecked) return@addOnButtonCheckedListener
			val mode = when (checkedId) {
				R.id.windowedButton -> MODE_WINDOWED
				R.id.fullscreenButton -> MODE_FULLSCREEN
				else -> MODE_BORDERLESS
			}
			prefs.edit().putString(KEY_DISPLAY_MODE, mode).apply()
			refreshDisplayModeButtons()
		}

		stretchResolutionSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_STRETCH_RESOLUTION, checked).apply()
		}

		keyboardResizesSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_KEYBOARD_RESIZES_SCREEN, checked).apply()
		}
	}

	private fun bindRuntime() = with(binding) {
		globalArgsInput.doAfterTextChanged {
			prefs.edit().putString(KEY_GLOBAL_ARGUMENTS, it?.toString().orEmpty()).apply()
		}

		fixFontSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_FIX_FONT, checked).apply()
		}

		enableDownloaderSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_ENABLE_DOWNLOADER, checked).apply()
		}

		enableYapbSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_ENABLE_YAPB_BOTS, checked).apply()
		}

		crashLogsButton.setOnClickListener {
			findNavController().navigate(R.id.action_appSettingsFragment_to_crashLogsFragment)
		}
	}

	private fun refreshAll() = with(binding) {
		useAndroidDataSwitch.isChecked = prefs.getBoolean(KEY_USE_ANDROID_DATA, false)
		gamePathInput.setText(prefs.getString(KEY_GAME_PATH, defaultGamePath()))
		gamePathInput.isEnabled = !useAndroidDataSwitch.isChecked

		globalArgsInput.setText(prefs.getString(KEY_GLOBAL_ARGUMENTS, ""))
		stretchResolutionSwitch.isChecked = prefs.getBoolean(KEY_STRETCH_RESOLUTION, false)
		keyboardResizesSwitch.isChecked = prefs.getBoolean(KEY_KEYBOARD_RESIZES_SCREEN, true)
		fixFontSwitch.isChecked = prefs.getBoolean(KEY_FIX_FONT, true)
		enableDownloaderSwitch.isChecked = prefs.getBoolean(KEY_ENABLE_DOWNLOADER, true)
		enableYapbSwitch.isChecked = prefs.getBoolean(KEY_ENABLE_YAPB_BOTS, false)

		refreshGamePathSummary()
		refreshResolutionSummary()
		refreshDisplayModeButtons()
	}

	private fun refreshGamePathSummary() = with(binding) {
		val summary = if (useAndroidDataSwitch.isChecked) {
			requireContext().getString(R.string.using_internal_storage)
		} else {
			val path = prefs.getString(KEY_GAME_PATH, defaultGamePath()).orEmpty().trim()
			if (path.isEmpty()) {
				requireContext().getString(R.string.using_external_storage)
			} else {
				File(path).absolutePath
			}
		}
		gamePathSummary.text = summary
	}

	private fun refreshResolutionSummary() = with(binding) {
		renderResolutionButton.text = getRenderResolutionButtonLabel()
		renderResolutionSummary.text = getRenderResolutionSummary()
	}

	private fun refreshDisplayModeButtons() = with(binding) {
		when (prefs.getString(KEY_DISPLAY_MODE, MODE_BORDERLESS)) {
			MODE_WINDOWED -> displayModeGroup.check(R.id.windowedButton)
			MODE_FULLSCREEN -> displayModeGroup.check(R.id.fullscreenButton)
			else -> displayModeGroup.check(R.id.borderlessButton)
		}
	}

	private fun getRenderResolutionSummary(): String {
		val current = prefs.getString(KEY_RENDER_RESOLUTION, "")?.trim().orEmpty()
		return if (current.isEmpty()) {
			getString(R.string.render_resolution_summary)
		} else {
			current
		}
	}

	private fun getRenderResolutionButtonLabel(): String {
		val current = prefs.getString(KEY_RENDER_RESOLUTION, "")?.trim().orEmpty()
		return if (current.isEmpty()) {
			getString(R.string.render_resolution)
		} else {
			"${getString(R.string.render_resolution)}: $current"
		}
	}

	private fun showRenderResolutionDialog() {
		val currentResolution = prefs.getString(KEY_RENDER_RESOLUTION, "")?.trim().orEmpty()
		val editText = EditText(requireContext()).apply {
			setText(currentResolution)
			hint = "e.g. 1280x720"
		}

		MaterialAlertDialogBuilder(requireContext())
			.setTitle(R.string.render_resolution_dialog)
			.setMessage(R.string.render_resolution_summary)
			.setView(editText)
			.setPositiveButton(android.R.string.ok) { _, _ ->
				val newValue = editText.text?.toString().orEmpty().trim()
				prefs.edit().putString(KEY_RENDER_RESOLUTION, newValue).apply()
				refreshResolutionSummary()
			}
			.setNegativeButton(android.R.string.cancel, null)
			.setNeutralButton(R.string.clear) { _, _ ->
				prefs.edit().remove(KEY_RENDER_RESOLUTION).apply()
				refreshResolutionSummary()
			}
			.show()
	}

	private fun defaultGamePath(): String {
		val external = android.os.Environment.getExternalStorageDirectory().absolutePath
		return "$external/xash"
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}

	companion object {
		private const val APP_PREFS = "app_preferences"
		private const val KEY_GAME_PATH = "game_path"
		private const val KEY_USE_ANDROID_DATA = "use_android_data"
		private const val KEY_GLOBAL_ARGUMENTS = "global_arguments"
		private const val KEY_RENDER_RESOLUTION = "render_resolution"
		private const val KEY_DISPLAY_MODE = "display_mode"
		private const val KEY_STRETCH_RESOLUTION = "stretch_resolution"
		private const val KEY_KEYBOARD_RESIZES_SCREEN = "keyboard_resizes_screen"
		private const val KEY_FIX_FONT = "fix_font"
		private const val KEY_ENABLE_DOWNLOADER = "enable_downloader"
		private const val KEY_ENABLE_YAPB_BOTS = "enable_yapb_bots"

		private const val MODE_WINDOWED = "windowed"
		private const val MODE_BORDERLESS = "borderless"
		private const val MODE_FULLSCREEN = "fullscreen"
	}
}
