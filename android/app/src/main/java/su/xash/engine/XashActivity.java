package su.xash.engine;

import android.annotation.SuppressLint;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings.Secure;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import su.xash.engine.util.AndroidBug5497Workaround;
import su.xash.engine.util.CrashReports;

import java.io.File;
import java.util.Arrays;
import java.util.List;

public class XashActivity extends SDLActivity {
	private boolean mUseVolumeKeys;
	private String mPackageName;
	private FrameLayout mMotdOverlay;
	private WebView mMotdWebView;
	private static final String TAG = "XashActivity";

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		if (handleStopDedicated(getIntent()))
			return;

		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
			//getWindow().addFlags(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES);
			getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
		}

		AndroidBug5497Workaround.assistActivity(this);
	}

	@Override
	protected void onNewIntent(Intent intent) {
		super.onNewIntent(intent);
		handleStopDedicated(intent);
	}

	private boolean handleStopDedicated(Intent intent) {
		if (intent != null && "su.xash.engine.STOP_DEDICATED".equals(intent.getAction())) {
			finish();
			return true;
		}
		return false;
	}

	@Override
	public void onDestroy() {
		super.onDestroy();

		// Now that we don't exit from native code, we need to exit here, resetting
		// application state (actually global variables that we don't cleanup on exit)
		//
		// When the issue with global variables will be resolved, remove that exit() call
		System.exit(0);
	}

	@Override
	protected String[] getLibraries() {
		return new String[]{"SDL2", "xash"};
	}

	@SuppressLint("HardwareIds")
	private String getAndroidID() {
		return Secure.getString(getContentResolver(), Secure.ANDROID_ID);
	}

	@SuppressLint("ApplySharedPref")
	private void saveAndroidID(String id) {
		getSharedPreferences("xash_preferences", MODE_PRIVATE).edit().putString("xash_id", id).commit();
	}

	private String loadAndroidID() {
		return getSharedPreferences("xash_preferences", MODE_PRIVATE).getString("xash_id", "");
	}

	private static native void nativeMotdClosed();

	private Typeface loadMotdTypeface() {
		try {
			return Typeface.createFromAsset(getAssets(), "gfx/fonts/cs_regular.ttf");
		} catch (Exception ignored) {
		}

		String[] paths = {
			Environment.getExternalStorageDirectory().getAbsolutePath() + "/xash/cstrike/gfx/fonts/cs_regular.ttf",
			Environment.getExternalStorageDirectory().getAbsolutePath() + "/xash/valve/gfx/fonts/cs_regular.ttf",
			getFilesDir().getAbsolutePath() + "/gamelibs/cstrike/gfx/fonts/cs_regular.ttf",
			getFilesDir().getAbsolutePath() + "/gamelibs/valve/gfx/fonts/cs_regular.ttf"
		};

		for (String path : paths) {
			try {
				File file = new File(path);
				if (file.exists() && file.length() > 0)
					return Typeface.createFromFile(file);
			} catch (Exception ignored) {
			}
		}

		return Typeface.DEFAULT_BOLD;
	}

	private String stripGoldSrcColorCodes(String text) {
		if (text == null || text.isEmpty())
			return "";

		StringBuilder clean = new StringBuilder(text.length());
		for (int i = 0; i < text.length(); i++) {
			char c = text.charAt(i);
			if (c == '^' && i + 1 < text.length()) {
				char next = text.charAt(i + 1);
				if (next >= '0' && next <= '9') {
					i++;
					continue;
				}
			}
			clean.append(c);
		}
		return clean.toString();
	}

	private GradientDrawable makeMotdOutline(int fillColor, int strokeWidth, int strokeColor, float radius) {
		GradientDrawable drawable = new GradientDrawable();
		drawable.setColor(fillColor);
		drawable.setStroke(strokeWidth, strokeColor);
		drawable.setCornerRadius(radius);
		return drawable;
	}

	private TextView makeMotdControl(String text, float textSize) {
		TextView control = new TextView(this);
		control.setText(text);
		control.setTextColor(Color.rgb(255, 190, 48));
		control.setTextSize(textSize);
		control.setGravity(Gravity.CENTER);
		control.setIncludeFontPadding(false);
		control.setClickable(true);
		control.setBackground(makeMotdOutline(Color.argb(173, 0, 0, 0), 1, Color.rgb(255, 190, 48), 4.0f));
		return control;
	}

	private void scrollMotdBy(int dx, int dy) {
		if (mMotdWebView == null)
			return;

		mMotdWebView.scrollBy(dx, dy);
		String script = "(function(){var e=document.scrollingElement||document.documentElement||document.body;"
			+ "if(e&&e.scrollBy)e.scrollBy(" + dx + "," + dy + ");"
			+ "else window.scrollBy(" + dx + "," + dy + ");})()";
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT)
			mMotdWebView.evaluateJavascript(script, null);
		else
			mMotdWebView.loadUrl("javascript:" + script);
	}

	private void updateMotdScrollControls(View up, View down, View verticalTrack, View left, View right, View horizontalTrack) {
		if (mMotdWebView == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT)
			return;

		String script = "(function(){var e=document.scrollingElement||document.documentElement||document.body;"
			+ "return [e.scrollWidth,e.clientWidth,e.scrollHeight,e.clientHeight].join(',');})()";
		mMotdWebView.evaluateJavascript(script, value -> {
			if (value == null)
				return;

			String clean = value.replace("\"", "");
			String[] parts = clean.split(",");
			if (parts.length != 4)
				return;

			try {
				int scrollWidth = (int)Float.parseFloat(parts[0]);
				int clientWidth = (int)Float.parseFloat(parts[1]);
				int scrollHeight = (int)Float.parseFloat(parts[2]);
				int clientHeight = (int)Float.parseFloat(parts[3]);
				int horizontalVisibility = scrollWidth > clientWidth + 2 ? View.VISIBLE : View.GONE;
				int verticalVisibility = scrollHeight > clientHeight + 2 ? View.VISIBLE : View.GONE;

				left.setVisibility(horizontalVisibility);
				right.setVisibility(horizontalVisibility);
				horizontalTrack.setVisibility(horizontalVisibility);
				up.setVisibility(verticalVisibility);
				down.setVisibility(verticalVisibility);
				verticalTrack.setVisibility(verticalVisibility);
			} catch (NumberFormatException ignored) {
			}
		});
	}

	@SuppressLint("SetJavaScriptEnabled")
	private void showMotdHtml(String html, String baseUrl, String serverName, int x, int y, int width, int height) {
		runOnUiThread(() -> {
			hideMotdHtml();

			FrameLayout content = findViewById(android.R.id.content);
			mMotdOverlay = new FrameLayout(this);
			mMotdOverlay.setBackgroundColor(Color.TRANSPARENT);
			mMotdOverlay.setClickable(false);
			mMotdOverlay.setFocusable(true);

			int displayWidth = getResources().getDisplayMetrics().widthPixels;
			int displayHeight = getResources().getDisplayMetrics().heightPixels;
			int panelWidth = width > 0 ? width : (int)(displayWidth * 0.64f);
			int panelHeight = height > 0 ? height : (int)(displayHeight * 0.76f);
			int panelX = x >= 0 ? x : (displayWidth - panelWidth) / 2;
			int panelY = y >= 0 ? y : (displayHeight - panelHeight) / 2;

			if (panelWidth > displayWidth - 40)
				panelWidth = displayWidth - 40;
			if (panelHeight > displayHeight - 40)
				panelHeight = displayHeight - 40;
			if (panelX < 20)
				panelX = 20;
			if (panelY < 20)
				panelY = 20;
			if (panelX + panelWidth > displayWidth - 20)
				panelX = displayWidth - panelWidth - 20;
			if (panelY + panelHeight > displayHeight - 20)
				panelY = displayHeight - panelHeight - 20;

			FrameLayout panel = new FrameLayout(this);
			panel.setClickable(true);
			GradientDrawable panelBackground = new GradientDrawable();
			panelBackground.setColor(Color.argb(179, 0, 0, 0));
			panelBackground.setCornerRadius(14.0f);
			panel.setBackground(panelBackground);
			FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
				panelWidth,
				panelHeight
			);
			panelParams.leftMargin = panelX;
			panelParams.topMargin = panelY;
			mMotdOverlay.addView(panel, panelParams);

			FrameLayout inner = new FrameLayout(this);
			inner.setBackground(makeMotdOutline(Color.TRANSPARENT, 1, Color.rgb(255, 190, 48), 10.0f));
			FrameLayout.LayoutParams innerParams = new FrameLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.MATCH_PARENT
			);
			innerParams.setMargins(24, 58, 48, 54);
			panel.addView(inner, innerParams);

			Typeface motdTypeface = loadMotdTypeface();

			TextView logo = new TextView(this);
			logo.setText("-");
			logo.setTypeface(motdTypeface);
			logo.setTextColor(Color.rgb(255, 190, 48));
			logo.setTextSize(22.0f);
			logo.setGravity(Gravity.CENTER);
			logo.setIncludeFontPadding(false);
			FrameLayout.LayoutParams logoParams = new FrameLayout.LayoutParams(44, 44, Gravity.LEFT | Gravity.TOP);
			logoParams.leftMargin = 24;
			logoParams.topMargin = 8;
			panel.addView(logo, logoParams);

			TextView title = new TextView(this);
			String cleanServerName = stripGoldSrcColorCodes(serverName).trim();
			String titleText = cleanServerName.isEmpty() ? "Message of the Day" : cleanServerName;
			title.setText(titleText);
			title.setTypeface(Typeface.DEFAULT_BOLD);
			title.setTextColor(Color.rgb(255, 190, 48));
			title.setTextSize(12.0f);
			title.setSingleLine(true);
			title.setGravity(Gravity.CENTER_VERTICAL);
			FrameLayout.LayoutParams titleParams = new FrameLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT,
				38,
				Gravity.LEFT | Gravity.TOP
			);
			titleParams.setMargins(70, 12, 64, 0);
			panel.addView(title, titleParams);

			mMotdWebView = new WebView(this);
			mMotdWebView.setBackgroundColor(Color.TRANSPARENT);

			WebSettings settings = mMotdWebView.getSettings();
			settings.setJavaScriptEnabled(true);
			settings.setDomStorageEnabled(true);
			settings.setLoadWithOverviewMode(true);
			settings.setUseWideViewPort(true);
			settings.setBuiltInZoomControls(false);
			settings.setDisplayZoomControls(false);
			settings.setAllowFileAccess(true);
			settings.setAllowContentAccess(true);
			mMotdWebView.setHorizontalScrollBarEnabled(false);
			mMotdWebView.setVerticalScrollBarEnabled(false);

			FrameLayout.LayoutParams webParams = new FrameLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.MATCH_PARENT
			);
			webParams.setMargins(10, 10, 10, 10);
			inner.addView(mMotdWebView, webParams);

			TextView up = makeMotdControl("^", 14.0f);
			up.setOnClickListener(v -> scrollMotdBy(0, -80));
			FrameLayout.LayoutParams upParams = new FrameLayout.LayoutParams(26, 26, Gravity.RIGHT | Gravity.TOP);
			upParams.setMargins(0, 58, 14, 0);
			panel.addView(up, upParams);

			TextView down = makeMotdControl("v", 14.0f);
			down.setOnClickListener(v -> scrollMotdBy(0, 80));
			FrameLayout.LayoutParams downParams = new FrameLayout.LayoutParams(26, 26, Gravity.RIGHT | Gravity.BOTTOM);
			downParams.setMargins(0, 0, 14, 54);
			panel.addView(down, downParams);

			TextView verticalTrack = new TextView(this);
			verticalTrack.setBackground(makeMotdOutline(Color.argb(173, 0, 0, 0), 1, Color.rgb(255, 190, 48), 4.0f));
			FrameLayout.LayoutParams verticalTrackParams = new FrameLayout.LayoutParams(26, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.RIGHT | Gravity.TOP);
			verticalTrackParams.setMargins(0, 86, 14, 84);
			panel.addView(verticalTrack, verticalTrackParams);

			TextView left = makeMotdControl("<", 16.0f);
			left.setOnClickListener(v -> scrollMotdBy(-80, 0));
			FrameLayout.LayoutParams leftParams = new FrameLayout.LayoutParams(26, 26, Gravity.LEFT | Gravity.BOTTOM);
			leftParams.setMargins(184, 0, 0, 26);
			panel.addView(left, leftParams);

			TextView right = makeMotdControl(">", 16.0f);
			right.setOnClickListener(v -> scrollMotdBy(80, 0));
			FrameLayout.LayoutParams rightParams = new FrameLayout.LayoutParams(26, 26, Gravity.RIGHT | Gravity.BOTTOM);
			rightParams.setMargins(0, 0, 48, 26);
			panel.addView(right, rightParams);

			TextView horizontalTrack = new TextView(this);
			horizontalTrack.setBackground(makeMotdOutline(Color.argb(173, 0, 0, 0), 1, Color.rgb(255, 190, 48), 4.0f));
			FrameLayout.LayoutParams horizontalTrackParams = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 26, Gravity.LEFT | Gravity.BOTTOM);
			horizontalTrackParams.setMargins(212, 0, 76, 26);
			panel.addView(horizontalTrack, horizontalTrackParams);

			up.setVisibility(View.GONE);
			down.setVisibility(View.GONE);
			verticalTrack.setVisibility(View.GONE);
			left.setVisibility(View.GONE);
			right.setVisibility(View.GONE);
			horizontalTrack.setVisibility(View.GONE);

			mMotdWebView.setWebViewClient(new WebViewClient() {
				@Override
				public void onPageFinished(WebView view, String url) {
					updateMotdScrollControls(up, down, verticalTrack, left, right, horizontalTrack);
					view.postDelayed(() -> updateMotdScrollControls(up, down, verticalTrack, left, right, horizontalTrack), 500);
					view.postDelayed(() -> updateMotdScrollControls(up, down, verticalTrack, left, right, horizontalTrack), 1500);
				}
			});

			TextView ok = makeMotdControl("OK", 12.0f);
			ok.setTypeface(Typeface.DEFAULT);
			ok.setBackground(makeMotdOutline(Color.argb(173, 0, 0, 0), 1, Color.rgb(255, 190, 48), 4.0f));
			ok.setOnClickListener(v -> {
				hideMotdHtml();
				nativeMotdClosed();
			});
			FrameLayout.LayoutParams okParams = new FrameLayout.LayoutParams(150, 30, Gravity.LEFT | Gravity.BOTTOM);
			okParams.leftMargin = 24;
			okParams.bottomMargin = 14;
			panel.addView(ok, okParams);

			content.addView(mMotdOverlay, new FrameLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.MATCH_PARENT
			));

			String data = html == null ? "" : html.trim();
			if (data.startsWith("http://") || data.startsWith("https://")) {
				mMotdWebView.loadUrl(data);
			} else {
				String resolvedBaseUrl = (baseUrl == null || baseUrl.isEmpty()) ? "https://xash-motd.local/" : baseUrl;
				mMotdWebView.loadDataWithBaseURL(resolvedBaseUrl, html == null ? "" : html, "text/html", "UTF-8", null);
			}
		});
	}

	private void hideMotdHtml() {
		runOnUiThread(() -> {
			FrameLayout content = findViewById(android.R.id.content);
			if (mMotdWebView != null) {
				mMotdWebView.stopLoading();
				mMotdWebView.destroy();
				mMotdWebView = null;
			}
			if (mMotdOverlay != null) {
				content.removeView(mMotdOverlay);
				mMotdOverlay = null;
			}
		});
	}

	@Override
	public String getCallingPackage() {
		if (mPackageName != null) {
			return mPackageName;
		}

		return super.getCallingPackage();
	}

	private AssetManager getAssets(boolean isEngine) {
		AssetManager am = null;

		if (isEngine) {
			am = getAssets();
		} else {
			try {
				am = getPackageManager().getResourcesForApplication(getCallingPackage()).getAssets();
			} catch (Exception e) {
				Log.e(TAG, "Unable to load mod assets!");
				e.printStackTrace();
			}
		}

		return am;
	}

	private String[] getAssetsList(boolean isEngine, String path) {
		AssetManager am = getAssets(isEngine);

		try {
			return am.list(path);
		} catch (Exception e) {
			e.printStackTrace();
		}

		return new String[]{};
	}

	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		if (SDLActivity.mBrokenLibraries) {
			return false;
		}

		int keyCode = event.getKeyCode();
		if (!mUseVolumeKeys) {
			if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_CAMERA || keyCode == KeyEvent.KEYCODE_ZOOM_IN || keyCode == KeyEvent.KEYCODE_ZOOM_OUT) {
				return false;
			}
		}

		return getWindow().superDispatchKeyEvent(event);
	}

	private static void appendStringExtra(StringBuilder sb, Intent intent, String key) {
		String value = intent.getStringExtra(key);
		if (value != null)
			sb.append("  ").append(key).append(" = ").append(value).append('\n');
	}

	// record intent info, so that it could be consumed later for crash reporting
	private void recordLaunchInfo() {
		// do not overwrite current launch info with pending crash log, shouldn't happen but might
		File pendingCrash = new File(getFilesDir(), "crashes/" + CrashReports.STACKTRACE_NAME);
		if (pendingCrash.exists() && pendingCrash.length() > 0)
			return;

		// write Android version, fingerprint, supported abis, etc
		CrashReports.writeSystemInfo(this);

		// now create intent info and pass it to crash reporting
		Intent intent = getIntent();
		if (intent == null)
			return;
		StringBuilder sb = new StringBuilder();
		sb.append("Action: ").append(intent.getAction()).append('\n');
		sb.append("Data: ").append(intent.getDataString()).append('\n');
		sb.append("Calling package: ").append(getCallingPackage()).append('\n');
		sb.append("Extras:\n");
		// only write intent extras that we care about
		appendStringExtra(sb, intent, "gamedir");
		appendStringExtra(sb, intent, "gamelibdir");
		appendStringExtra(sb, intent, "pakfile");
		appendStringExtra(sb, intent, "basedir");
		appendStringExtra(sb, intent, "package");
		appendStringExtra(sb, intent, "argv");
		sb.append("  usevolume = ").append(intent.getBooleanExtra("usevolume", false)).append('\n');
		String[] env = intent.getStringArrayExtra("env");
		if (env != null)
			sb.append("  env = ").append(Arrays.toString(env)).append('\n');
		CrashReports.writeIntentInfo(this, sb.toString());
	}

	// TODO: REMOVE LATER, temporary launchers support?
	@Override
	protected String[] getArguments() {
		File crashDir = new File(getFilesDir(), "crashes");
		crashDir.mkdirs();
		nativeSetenv("XASH3D_CRASH_DIR", crashDir.getAbsolutePath());

		recordLaunchInfo();

		String gamedir = getIntent().getStringExtra("gamedir");
		if (gamedir == null) gamedir = "valve";
		nativeSetenv("XASH3D_GAME", gamedir);

		String gamelibdir = getIntent().getStringExtra("gamelibdir");
		if (gamelibdir != null) nativeSetenv("XASH3D_GAMELIBDIR", gamelibdir);

		String rodir = System.getenv("XASH3D_RODIR");
		if (rodir == null) {
			// FIXME: we are using rodir as a supplier for downloaded game libraries
			rodir = getFilesDir().getAbsolutePath() + "/gamelibs";
			nativeSetenv("XASH3D_RODIR", rodir);
		}
		Log.i(TAG, "XASH3D_RODIR = " + rodir);

		String pakfile = getIntent().getStringExtra("pakfile");
		if (pakfile != null) nativeSetenv("XASH3D_EXTRAS_PAK2", pakfile);

		String basedir = getIntent().getStringExtra("basedir");
		if (basedir != null) {
			nativeSetenv("XASH3D_BASEDIR", basedir);
		} else {
			String rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/xash";
			nativeSetenv("XASH3D_BASEDIR", rootPath);
		}

		mUseVolumeKeys = getIntent().getBooleanExtra("usevolume", false);
		mPackageName = getIntent().getStringExtra("package");

		String[] env = getIntent().getStringArrayExtra("env");
		if (env != null) {
			for (int i = 0; i < env.length; i += 2)
				nativeSetenv(env[i], env[i + 1]);
		}

		String argv = getIntent().getStringExtra("argv");
		if (argv == null) argv = "-console -log";

		return argv.split(" ");
	}
}
