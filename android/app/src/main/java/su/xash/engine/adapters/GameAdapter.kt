package su.xash.engine.adapters

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.AnimationUtils
import android.widget.PopupMenu
import androidx.navigation.findNavController
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import su.xash.engine.R
import su.xash.engine.databinding.CardGameBinding
import su.xash.engine.model.Game
import su.xash.engine.ui.library.LibraryViewModel


class GameAdapter(
	private val libraryViewModel: LibraryViewModel,
	private val isGrid: Boolean
) : ListAdapter<Game, GameAdapter.GameViewHolder>(DiffCallback()) {

	private var lastPosition = -1

	override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameAdapter.GameViewHolder {
		val binding = CardGameBinding.inflate(LayoutInflater.from(parent.context), parent, false)
		return GameViewHolder(binding)
	}

	override fun onBindViewHolder(holder: GameAdapter.GameViewHolder, position: Int) {
		holder.bind(getItem(position))
		setAnimation(holder.itemView, position)
	}

	private fun setAnimation(view: View, position: Int) {
		if (position > lastPosition) {
			view.startAnimation(AnimationUtils.loadAnimation(view.context, android.R.anim.fade_in))
			lastPosition = position
		}
	}

	private class DiffCallback : DiffUtil.ItemCallback<Game>() {
		override fun areItemsTheSame(oldItem: Game, newItem: Game): Boolean {
			return oldItem.basedir.name == newItem.basedir.name
		}

		override fun areContentsTheSame(oldItem: Game, newItem: Game): Boolean {
			return oldItem.title == newItem.title
				&& (oldItem.icon == null && newItem.icon == null
					|| oldItem.icon?.sameAs(newItem.icon) == true)
		}
	}

	inner class GameViewHolder(val binding: CardGameBinding) :
		RecyclerView.ViewHolder(binding.root) {
		fun bind(game: Game) {
			binding.apply {
				gameTitle.text = game.title
				gameSubtitle.text = game.basedir.name

				if (game.icon != null) {
					gameIcon.setImageBitmap(game.icon)
					gameIcon.visibility = View.VISIBLE
				} else {
					gameIcon.visibility = View.GONE
				}

				if (game.cover != null) {
					gameCover.setImageBitmap(game.cover)
					gameCover.visibility = View.VISIBLE
				} else {
					gameCover.visibility = View.GONE
				}

				settingsButton.setOnClickListener {
					libraryViewModel.setSelectedGame(game)
					it.findNavController()
						.navigate(R.id.action_libraryFragment_to_gameSettingsFragment)
				}

				root.setOnClickListener { libraryViewModel.startEngine(it.context, game) }
				launchButton.setOnClickListener {
					libraryViewModel.startEngine(it.context, game)
				}

				root.setOnLongClickListener { v ->
					showContextMenu(v, game)
					true
				}
			}
		}

		private fun showContextMenu(view: View, game: Game) {
			val popup = PopupMenu(view.context, view)
			popup.menu.add(0, 1, 0, R.string.context_start)
			popup.menu.add(0, 2, 0, R.string.context_settings)
			popup.menu.add(0, 3, 0, R.string.open_game_folder)
			popup.setOnMenuItemClickListener { item ->
				when (item.itemId) {
					1 -> { libraryViewModel.startEngine(view.context, game); true }
					2 -> {
						libraryViewModel.setSelectedGame(game)
						view.findNavController()
							.navigate(R.id.action_libraryFragment_to_gameSettingsFragment)
						true
					}
					else -> false
				}
			}
			popup.show()
		}
	}
}
