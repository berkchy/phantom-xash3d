package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.navigation.fragment.findNavController
import androidx.preference.EditTextPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import su.xash.engine.R

class AppSettingsPreferenceFragment() : PreferenceFragmentCompat() {
	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceManager.sharedPreferencesName = "app_preferences";
		setPreferencesFromResource(R.xml.app_preferences, rootKey);

		val gamePath = findPreference<EditTextPreference>("game_path")
		val useAndroidData = findPreference<SwitchPreferenceCompat>("use_android_data")

		useAndroidData?.setOnPreferenceChangeListener { _, newValue ->
			gamePath?.isEnabled = newValue != true
			true
		}

		gamePath?.isEnabled = useAndroidData?.isChecked != true

		findPreference<Preference>("crash_logs")?.setOnPreferenceClickListener {
			findNavController().navigate(R.id.action_appSettingsFragment_to_crashLogsFragment)
			true
		}
	}
}
