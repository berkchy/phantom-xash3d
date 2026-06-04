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
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.widget.SearchView
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.MenuProvider
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.adapters.GameAdapter
import su.xash.engine.databinding.FragmentLibraryBinding


class LibraryFragment : Fragment(), MenuProvider {
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

		requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		binding.swipeRefresh.setOnRefreshListener { libraryViewModel.reloadGames(requireContext()) }

		libraryViewModel.isReloading.observe(viewLifecycleOwner) {
			binding.swipeRefresh.isRefreshing = it
		}

		libraryViewModel.installedGames.observe(viewLifecycleOwner) { games ->
			gameAdapter?.submitList(games)
			binding.emptyState.visibility = if (games.isEmpty()) View.VISIBLE else View.GONE
			binding.gamesList.visibility = if (games.isEmpty()) View.GONE else View.VISIBLE
		}

		binding.goToSettingsButton.setOnClickListener {
			findNavController().navigate(R.id.action_libraryFragment_to_appSettingsFragment)
		}

		if (checkStoragePermissions()) {
			libraryViewModel.reloadGames(requireContext())
		}
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}

	override fun onCreateMenu(menu: Menu, menuInflater: MenuInflater) {
		menuInflater.inflate(R.menu.menu_library, menu)

		val searchItem = menu.findItem(R.id.action_search)
		val searchView = searchItem?.actionView as? SearchView
		searchView?.queryHint = getString(R.string.search_games)
		searchView?.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
			override fun onQueryTextSubmit(query: String?): Boolean {
				libraryViewModel.updateSearchQuery(query ?: "")
				return true
			}

			override fun onQueryTextChange(newText: String?): Boolean {
				libraryViewModel.updateSearchQuery(newText ?: "")
				return true
			}
		})
		searchView?.setOnCloseListener {
			libraryViewModel.updateSearchQuery("")
			false
		}
	}

	override fun onMenuItemSelected(menuItem: MenuItem): Boolean {
		when (menuItem.itemId) {
			R.id.action_settings -> {
				findNavController().navigate(R.id.action_libraryFragment_to_appSettingsFragment)
				return true
			}
			R.id.action_toggle_view -> toggleViewMode(menuItem)
			R.id.action_sort_name_asc -> libraryViewModel.updateSortOrder(SortOrder.NAME_ASC)
			R.id.action_sort_name_desc -> libraryViewModel.updateSortOrder(SortOrder.NAME_DESC)
			R.id.action_dedicated_server -> {
				findNavController().navigate(R.id.action_libraryFragment_to_dedicatedServerFragment)
				return true
			}
		}
		return false
	}

	private fun toggleViewMode(item: MenuItem) {
		libraryViewModel.toggleViewMode()
		val isGrid = libraryViewModel.isGridView
		item.icon = if (isGrid) {
			requireContext().getDrawable(R.drawable.ic_baseline_view_list_24)
		} else {
			requireContext().getDrawable(R.drawable.ic_baseline_view_module_24)
		}
		item.title = if (isGrid) getString(R.string.list_view) else getString(R.string.grid_view)

		val adapter = GameAdapter(libraryViewModel, isGrid)
		gameAdapter = adapter
		binding.gamesList.adapter = adapter
		binding.gamesList.layoutManager = if (isGrid) {
			GridLayoutManager(requireContext(), 2)
		} else {
			LinearLayoutManager(requireContext())
		}
		libraryViewModel.installedGames.value?.let { adapter.submitList(it) }
	}

	override fun onResume() {
		super.onResume()

		if (checkStoragePermissions()) {
			libraryViewModel.reloadGames(requireContext())
		}
	}
}
