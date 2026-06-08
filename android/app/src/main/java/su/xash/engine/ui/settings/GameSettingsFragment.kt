package su.xash.engine.ui.settings

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.core.net.toUri
import su.xash.engine.R
import su.xash.engine.databinding.FragmentGameSettingsBinding
import su.xash.engine.model.GameLibDownloader
import su.xash.engine.ui.library.LibraryViewModel
import java.text.DateFormat
import java.util.Date

class GameSettingsFragment : Fragment() {
	private var _binding: FragmentGameSettingsBinding? = null
	private val binding get() = _binding!!

	private val libraryViewModel: LibraryViewModel by activityViewModels()

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentGameSettingsBinding.inflate(inflater, container, false)
		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		super.onViewCreated(view, savedInstanceState)

		val game = libraryViewModel.selectedItem.value!!
		val prefs = requireContext().getSharedPreferences(game.basedir.name, Context.MODE_PRIVATE)

		binding.gameTitle.text = game.title
		binding.gameSubtitle.text = game.basedir.name

		if (game.icon != null) {
			binding.gameIcon.setImageBitmap(game.icon)
			binding.gameIcon.visibility = View.VISIBLE
		} else {
			binding.gameIcon.visibility = View.GONE
		}

		if (game.cover != null) {
			binding.gameCover.setImageBitmap(game.cover)
			binding.gameCover.visibility = View.VISIBLE
		} else {
			binding.gameCover.visibility = View.GONE
		}

		binding.argumentsInput.setText(prefs.getString(KEY_ARGUMENTS, "-console -log"))
		binding.useVolumeButtonsSwitch.isChecked = prefs.getBoolean(KEY_USE_VOLUME_BUTTONS, false)
		binding.enableYapbSwitch.isChecked = prefs.getBoolean(KEY_ENABLE_YAPB_BOTS, true)
		binding.enableYapbSwitch.visibility =
			if (game.basedir.name.equals("cstrike", ignoreCase = true)
				|| game.basedir.name.equals("czero", ignoreCase = true)) {
				View.VISIBLE
			} else {
				View.GONE
			}

		binding.argumentsInput.doAfterTextChanged {
			prefs.edit().putString(KEY_ARGUMENTS, it?.toString().orEmpty().trim()).apply()
		}

		binding.useVolumeButtonsSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_USE_VOLUME_BUTTONS, checked).apply()
		}

		binding.enableYapbSwitch.setOnCheckedChangeListener { _, checked ->
			prefs.edit().putBoolean(KEY_ENABLE_YAPB_BOTS, checked).apply()
		}

		populateBuildInfo(game.basedir.name)
		bindBuildInfoActions(game.basedir.name)
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}

	private fun populateBuildInfo(gameDir: String) {
		val downloader = GameLibDownloader(requireContext())
		val source = downloader.getSourceInfo(gameDir)
		val downloadedAt = downloader.getDownloadTime(gameDir)

		binding.sourceUrlValue.text = source?.url ?: "—"
		binding.sourceBranchValue.text = getString(R.string.source_branch) + ": " + (source?.branch ?: "—")
		binding.sourceCommitValue.text = getString(R.string.source_commit) + ": " + (source?.commit ?: "—")
		binding.downloadedAtValue.text = if (downloadedAt > 0L) {
			getString(R.string.downloaded_at) + ": " + DateFormat.getDateTimeInstance().format(Date(downloadedAt))
		} else {
			getString(R.string.downloaded_at) + ": —"
		}
	}

	private fun bindBuildInfoActions(gameDir: String) = with(binding) {
		val downloader = GameLibDownloader(requireContext())
		val source = downloader.getSourceInfo(gameDir)

		sourceUrlValue.setOnClickListener {
			source?.url?.let { url ->
				startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, url.toUri()))
			}
		}
		sourceCommitValue.setOnClickListener {
			source?.let {
				val target = it.url?.trimEnd('/')?.let { url -> "$url/commit/${it.commit}" }
				if (target != null) {
					startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, target.toUri()))
				}
			}
		}
	}

	companion object {
		private const val KEY_ARGUMENTS = "arguments"
		private const val KEY_USE_VOLUME_BUTTONS = "use_volume_buttons"
		private const val KEY_ENABLE_YAPB_BOTS = "enable_yapb_bots"
	}
}
