package su.xash.engine.ui.library

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.button.MaterialButtonToggleGroup
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.adapters.GameAdapter
import su.xash.engine.databinding.FragmentLibraryBinding
import android.text.Editable
import android.text.TextWatcher


class LibraryFragment : Fragment() {
	private var _binding: FragmentLibraryBinding? = null
	private val binding get() = _binding!!

	private val libraryViewModel: LibraryViewModel by activityViewModels()
	private var gameAdapter: GameAdapter? = null

	private val startActivityForResult =
		registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
			if (checkStoragePermissions()) {
				libraryViewModel.reloadGames(requireContext())
			}
		}

	private val requiredPermissions = arrayOf(
		Manifest.permission.READ_EXTERNAL_STORAGE,
		Manifest.permission.WRITE_EXTERNAL_STORAGE
	)

	private val requestPermissionLauncher = registerForActivityResult(
		ActivityResultContracts.RequestMultiplePermissions()
	) { permissions ->
		val granted = permissions.entries.all { it.value }
		if (granted) {
			libraryViewModel.reloadGames(requireContext())
		} else {
			checkStoragePermissions()
		}
	}

	private fun checkStoragePermissions(): Boolean {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			if (!Environment.isExternalStorageManager()) {
				MaterialAlertDialogBuilder(requireContext()).apply {
					setTitle(R.string.file_access_required)
					setMessage(R.string.file_access_message)
					setPositiveButton(R.string.open_settings) { _, _ ->
						startActivityForResult.launch(
							Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).setData(
								Uri.fromParts("package", BuildConfig.APPLICATION_ID, null)
							)
						)
					}
					setNeutralButton(R.string.done_check_permissions) { _, _ ->
						checkStoragePermissions()
					}
					setCancelable(false)
					show()

					return false
				}
			} else {
				return true
			}
		} else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			val permissionsNeeded = requiredPermissions.filter {
				ContextCompat.checkSelfPermission(
					requireContext(),
					it
				) != PackageManager.PERMISSION_GRANTED
			}.toTypedArray()

			if (!permissionsNeeded.isEmpty()) {
				val showRationale = permissionsNeeded.any {
					ActivityCompat.shouldShowRequestPermissionRationale(requireActivity(), it)
				}

				MaterialAlertDialogBuilder(requireContext()).apply {
					setTitle(R.string.external_storage_required)
					setMessage(R.string.external_storage_message)
					setPositiveButton(R.string.open_settings) { _, _ ->
						if (showRationale) {
							requestPermissionLauncher.launch(permissionsNeeded)
						} else {
							val intent =
								Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
									data =
										Uri.fromParts("package", requireContext().packageName, null)
								}
							startActivity(intent)
						}
					}
					setNeutralButton(R.string.done_check_permissions) { _, _ ->
						checkStoragePermissions()
					}
					setCancelable(false)
					show()
				}

				return false
			} else {
				return true
			}
		} else {
			return true
		}
	}

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentLibraryBinding.inflate(inflater, container, false)

		gameAdapter = GameAdapter(libraryViewModel, false)
		binding.gamesList.adapter = gameAdapter
		binding.gamesList.layoutManager = LinearLayoutManager(requireContext())
		binding.gamesList.clipToPadding = false
		binding.gamesList.setPadding(
			binding.gamesList.paddingLeft,
			binding.gamesList.paddingTop,
			binding.gamesList.paddingRight,
			(resources.displayMetrics.density * 128f).toInt()
		)

		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		binding.swipeRefresh.setOnRefreshListener { libraryViewModel.reloadGames(requireContext()) }

		libraryViewModel.isReloading.observe(viewLifecycleOwner) {
			binding.swipeRefresh.isRefreshing = it
		}

		libraryViewModel.installedGames.observe(viewLifecycleOwner) { games ->
			gameAdapter?.submitList(games)
			binding.libraryCount.text = resources.getQuantityString(
				R.plurals.library_games_count,
				games.size,
				games.size
			)
			binding.emptyState.visibility = if (games.isEmpty()) View.VISIBLE else View.GONE
			binding.gamesList.visibility = if (games.isEmpty()) View.GONE else View.VISIBLE
		}

		binding.searchInput.addTextChangedListener(object : TextWatcher {
			override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
			override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
			override fun afterTextChanged(s: Editable?) {
				libraryViewModel.updateSearchQuery(s?.toString().orEmpty())
			}
		})

		binding.viewToggleGroup.addOnButtonCheckedListener { _: MaterialButtonToggleGroup, checkedId: Int, isChecked: Boolean ->
			if (!isChecked) return@addOnButtonCheckedListener
			val newGrid = checkedId == R.id.gridViewButton
			if (libraryViewModel.isGridView != newGrid) {
				libraryViewModel.toggleViewMode()
				swapLayoutManager(newGrid)
			}
		}

		binding.sortToggleGroup.addOnButtonCheckedListener { _: MaterialButtonToggleGroup, checkedId: Int, isChecked: Boolean ->
			if (!isChecked) return@addOnButtonCheckedListener
			when (checkedId) {
				R.id.sortAscButton -> libraryViewModel.updateSortOrder(SortOrder.NAME_ASC)
				R.id.sortDescButton -> libraryViewModel.updateSortOrder(SortOrder.NAME_DESC)
			}
		}

		binding.goToSettingsButton.setOnClickListener {
			findNavController().navigate(R.id.action_libraryFragment_to_appSettingsFragment)
		}

		binding.viewToggleGroup.check(
			if (libraryViewModel.isGridView) R.id.gridViewButton else R.id.listViewButton
		)
		binding.sortToggleGroup.check(
			if (libraryViewModel.sortOrder == SortOrder.NAME_ASC) R.id.sortAscButton else R.id.sortDescButton
		)
		binding.searchInput.setText(libraryViewModel.searchQuery)

		if (checkStoragePermissions()) {
			libraryViewModel.reloadGames(requireContext())
		}
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}

	private fun swapLayoutManager(isGrid: Boolean) {
		val adapter = GameAdapter(libraryViewModel, isGrid)
		gameAdapter = adapter
		binding.gamesList.adapter = adapter
		binding.gamesList.layoutManager = if (isGrid) {
			GridLayoutManager(requireContext(), calculateGridSpanCount())
		} else {
			LinearLayoutManager(requireContext())
		}
		libraryViewModel.installedGames.value?.let { adapter.submitList(it) }
	}

	private fun calculateGridSpanCount(): Int {
		val widthDp = resources.configuration.screenWidthDp
		return when {
			widthDp < 480 -> 1
			widthDp < 840 -> 2
			else -> 3
		}
	}

	override fun onResume() {
		super.onResume()

		if (checkStoragePermissions()) {
			libraryViewModel.reloadGames(requireContext())
		}
	}
}
