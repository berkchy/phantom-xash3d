package su.xash.engine.ui.library

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import android.os.Environment
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.model.Game
import su.xash.engine.util.Nomedia
import java.io.File

enum class SortOrder { NAME_ASC, NAME_DESC }

class LibraryViewModel(application: Application) : AndroidViewModel(application) {
	val installedGames: LiveData<List<Game>> get() = _installedGames
	private val _installedGames = MutableLiveData(emptyList<Game>())

	val isReloading: LiveData<Boolean> get() = _isReloading
	private val _isReloading = MutableLiveData(false)

	val selectedItem: LiveData<Game> get() = _selectedItem
	private val _selectedItem = MutableLiveData<Game>()

	var searchQuery: String = ""
	var sortOrder: SortOrder = SortOrder.NAME_ASC
	var isGridView: Boolean = false

	private var allGames: List<Game> = emptyList()
	private var searchJob: Job? = null

	private val appPreferences: SharedPreferences =
		application.getSharedPreferences("app_preferences", Context.MODE_PRIVATE)

	fun reloadGames(ctx: Context) {
		if (isReloading.value == true) {
			return
		}
		_isReloading.value = true

		viewModelScope.launch {
			withContext(Dispatchers.IO) {
				val useAndroidData = appPreferences.getBoolean("use_android_data", false)
				val rootPath = if (useAndroidData) {
					ctx.getExternalFilesDir(null)?.absolutePath + "/xash"
				} else {
					appPreferences.getString("game_path", null)
						?: (Environment.getExternalStorageDirectory().absolutePath + "/xash")
				}
				val root = File(rootPath)

				Nomedia.ensureNomedia(root)

				allGames = Game.getGames(ctx, root)
				applyFilterAndSort()
				_isReloading.postValue(false)
			}
		}
	}

	fun updateSearchQuery(query: String) {
		searchQuery = query
		searchJob?.cancel()
		searchJob = viewModelScope.launch {
			delay(200)
			applyFilterAndSort()
		}
	}

	fun updateSortOrder(order: SortOrder) {
		sortOrder = order
		applyFilterAndSort()
	}

	fun toggleViewMode() {
		isGridView = !isGridView
	}

	private fun applyFilterAndSort() {
		val filtered = allGames.filter {
			searchQuery.isBlank() || it.title.contains(searchQuery, ignoreCase = true)
		}
		val sorted = when (sortOrder) {
			SortOrder.NAME_ASC -> filtered.sortedBy { it.title }
			SortOrder.NAME_DESC -> filtered.sortedByDescending { it.title }
		}
		_installedGames.postValue(sorted)
	}

	fun setSelectedGame(game: Game) {
		_selectedItem.value = game
	}

	fun startEngine(ctx: Context, game: Game) {
		game.startEngine(ctx)
	}
}
