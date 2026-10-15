package com.modrix.overlay;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Renderizador do painel do Modrix: Views Android presas à janela da Activity
 * do jogo (sem permissão de sobreposição).
 *
 * Não decide nada: lê o view-model (JSON) que o PanelController em C++ produz
 * e devolve os toques como ações. Todo texto, cor e medida vem do JSON.
 *
 * Toque: o FrameLayout raiz não é clicável, então toques fora do botão e do
 * painel seguem para a View do Unity, que está embaixo. O painel e o botão
 * consomem os toques que caem neles, e o jogo não os recebe.
 */
public final class ModrixOverlay {
    static native String nativeSnapshot(long handle);
    static native boolean nativeAction(long handle, String action, String arg);
    static native boolean nativeTick(long handle, long nowMs);

    private static final long TICK_MS = 250;

    private final Activity activity;
    private final long handle;
    private final float density;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private FrameLayout root;
    private TextView fab;
    private LinearLayout panel;
    private int panelMaxH;  // altura máxima; o painel encolhe para caber no conteúdo
    private LinearLayout content;
    private LinearLayout tabsRow;
    private long renderedVersion = -1;
    private JSONObject theme;

    private ModrixOverlay(Activity activity, long handle) {
        this.activity = activity;
        this.handle = handle;
        this.density = activity.getResources().getDisplayMetrics().density;
    }

    /** Chamado pelo C++ (qualquer thread): monta o overlay na thread de UI. */
    public static void install(final Activity activity, final long handle) {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                new ModrixOverlay(activity, handle).attach();
            }
        });
    }

    // ------------------------------------------------------------------------
    private void attach() {
        root = new FrameLayout(activity);
        root.setClickable(false);  // não consome: toque fora do painel vai pro jogo
        root.setFocusable(false);
        activity.addContentView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        JSONObject snap = snapshot();
        if (snap == null) return;
        theme = snap.optJSONObject("theme");
        buildFab();
        buildPanel();
        render(snap);
        handler.postDelayed(ticker, TICK_MS);
    }

    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            if (nativeTick(handle, SystemClock.uptimeMillis())) {
                JSONObject snap = snapshot();
                if (snap != null) render(snap);
            }
            handler.postDelayed(this, TICK_MS);
        }
    };

    private JSONObject snapshot() {
        try {
            return new JSONObject(nativeSnapshot(handle));
        } catch (JSONException e) {
            return null;
        }
    }

    private void act(String action, String arg) {
        nativeAction(handle, action, arg);
        nativeTick(handle, SystemClock.uptimeMillis());  // resposta imediata ao toque
        JSONObject snap = snapshot();
        if (snap != null) render(snap);
    }

    // ------------------------------------------------------------------------
    // Botão flutuante: toque abre/fecha; arrastar move.
    private void buildFab() {
        int size = dp(theme.optInt("fab_size_dp", 44));
        int margin = dp(theme.optInt("margin_dp", 12));
        fab = new TextView(activity);
        fab.setGravity(Gravity.CENTER);
        fab.setTextColor(color("text"));
        fab.setTypeface(Typeface.DEFAULT_BOLD);
        fab.setTextSize(TypedValue.COMPLEX_UNIT_SP, theme.optInt("title_sp", 16));
        GradientDrawable bg = new GradientDrawable();
        bg.setShape(GradientDrawable.OVAL);
        bg.setColor(color("fab_bg"));
        bg.setStroke(dp(2), color("accent"));
        fab.setBackground(bg);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(size, size, Gravity.TOP | Gravity.START);
        lp.leftMargin = margin;
        lp.topMargin = margin * 6;
        root.addView(fab, lp);

        fab.setOnTouchListener(new View.OnTouchListener() {
            float downX, downY, startLeft, startTop;
            boolean dragging;
            @Override public boolean onTouch(View v, MotionEvent e) {
                FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) v.getLayoutParams();
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        downX = e.getRawX(); downY = e.getRawY();
                        startLeft = p.leftMargin; startTop = p.topMargin;
                        dragging = false;
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        float dx = e.getRawX() - downX, dy = e.getRawY() - downY;
                        if (!dragging && Math.hypot(dx, dy) > dp(8)) dragging = true;
                        if (dragging) {
                            p.leftMargin = Math.max(0, (int) (startLeft + dx));
                            p.topMargin = Math.max(0, (int) (startTop + dy));
                            v.setLayoutParams(p);
                            positionPanel();
                        }
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (!dragging) act("toggle_open", "");
                        return true;
                    default:
                        return true;  // consome tudo que começou no botão
                }
            }
        });
    }

    private void buildPanel() {
        panel = new LinearLayout(activity) {
            @Override protected void onMeasure(int widthSpec, int heightSpec) {
                // Altura do conteúdo, limitada a panelMaxH; a ScrollView (peso 1) fica com o que sobrar.
                super.onMeasure(widthSpec, MeasureSpec.makeMeasureSpec(panelMaxH, MeasureSpec.AT_MOST));
            }
        };
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setClickable(true);  // toque em área vazia do painel não vaza para o jogo
        panel.setBackground(rounded(color("panel_bg"), theme.optInt("corner_dp", 12), color("panel_border")));
        int pad = dp(10);
        panel.setPadding(pad, pad, pad, pad);

        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text("Modrix", "title_sp", "accent", true);
        header.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView close = text("✕", "title_sp", "text_dim", false);
        close.setPadding(dp(10), dp(2), dp(4), dp(2));
        close.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("close", ""); }
        });
        header.addView(close);
        panel.addView(header);

        tabsRow = new LinearLayout(activity);
        tabsRow.setPadding(0, dp(8), 0, dp(8));
        panel.addView(tabsRow);

        ScrollView scroll = new ScrollView(activity);
        content = new LinearLayout(activity);
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content);
        panel.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                dp(theme.optInt("panel_width_dp", 340)), ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
        root.addView(panel, lp);
        positionPanel();
    }

    /** Painel ao lado do botão, sem sair da tela. */
    private void positionPanel() {
        if (panel == null) return;
        FrameLayout.LayoutParams f = (FrameLayout.LayoutParams) fab.getLayoutParams();
        FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) panel.getLayoutParams();
        int margin = dp(theme.optInt("margin_dp", 12));
        int screenW = activity.getResources().getDisplayMetrics().widthPixels;
        int screenH = activity.getResources().getDisplayMetrics().heightPixels;
        panelMaxH = Math.min(dp(theme.optInt("panel_max_height_dp", 440)), screenH - 2 * margin);
        p.leftMargin = Math.min(f.leftMargin + f.width + margin, Math.max(0, screenW - p.width - margin));
        p.topMargin = Math.max(margin, Math.min(f.topMargin, screenH - panelMaxH - margin));
        panel.setLayoutParams(p);
    }

    // ------------------------------------------------------------------------
    private void render(JSONObject s) {
        long version = s.optLong("version");
        if (version == renderedVersion) return;
        renderedVersion = version;

        fab.setText(s.optJSONObject("fab") != null ? s.optJSONObject("fab").optString("label", "M") : "M");
        boolean open = s.optBoolean("open");
        panel.setVisibility(open ? View.VISIBLE : View.GONE);
        if (!open) return;

        tabsRow.removeAllViews();
        JSONArray tabs = s.optJSONArray("tabs");
        for (int i = 0; tabs != null && i < tabs.length(); i++) {
            final JSONObject t = tabs.optJSONObject(i);
            boolean active = t.optBoolean("active");
            TextView tab = text(t.optString("label"), "text_sp", active ? "text" : "text_dim", active);
            tab.setGravity(Gravity.CENTER);
            tab.setPadding(dp(10), dp(6), dp(10), dp(6));
            if (active) tab.setBackground(rounded(color("tab_active_bg"), 8, 0));
            tab.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { act("tab", t.optString("id")); }
            });
            tabsRow.addView(tab, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }

        content.removeAllViews();
        if ("mods".equals(s.optString("tab"))) renderMods(s.optJSONObject("mods"));
        else renderChecklist(s.optJSONObject("checklist"));
    }

    private void renderMods(JSONObject mods) {
        if (mods == null) return;
        String notice = mods.optString("notice");
        if (!TextUtils.isEmpty(notice)) {
            TextView n = text(notice, "small_sp", "accent", false);
            n.setPadding(dp(4), 0, dp(4), dp(8));
            content.addView(n);
        }
        JSONArray rows = mods.optJSONArray("rows");
        for (int i = 0; rows != null && i < rows.length(); i++) {
            final JSONObject r = rows.optJSONObject(i);
            LinearLayout row = new LinearLayout(activity);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(10), dp(8), dp(10), dp(8));
            row.setBackground(rounded(color("row_bg"), 8, 0));

            LinearLayout texts = new LinearLayout(activity);
            texts.setOrientation(LinearLayout.VERTICAL);
            texts.addView(line(r.optString("title"), "text_sp", "text", true));
            String state = r.optString("state");
            String stateColor = "loaded".equals(state) ? "text_dim" : ("disabled".equals(state) ? "unknown" : "error");
            texts.addView(line(r.optString("subtitle"), "small_sp", stateColor, false));
            if (!TextUtils.isEmpty(r.optString("detail")))
                texts.addView(line(r.optString("detail"), "small_sp", "error", false));
            if (!TextUtils.isEmpty(r.optString("pending")))
                texts.addView(line(r.optString("pending"), "small_sp", "accent", false));
            row.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

            View sw = toggle(r.optBoolean("enabled"));
            sw.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { act("toggle_mod", r.optString("id")); }
            });
            row.addView(sw);
            addRow(row, 6);
        }
    }

    private void renderChecklist(JSONObject c) {
        if (c == null) return;
        if (!"ready".equals(c.optString("mode"))) {
            TextView msg = text(c.optString("message"), "text_sp", "text_dim", false);
            msg.setGravity(Gravity.CENTER);
            msg.setPadding(dp(8), dp(48), dp(8), dp(48));
            content.addView(msg);
            return;
        }
        LinearLayout chips = new LinearLayout(activity);
        JSONArray summary = c.optJSONArray("summary");
        for (int i = 0; summary != null && i < summary.length(); i++) {
            JSONObject s = summary.optJSONObject(i);
            LinearLayout chip = new LinearLayout(activity);
            chip.setOrientation(LinearLayout.VERTICAL);
            chip.setGravity(Gravity.CENTER);
            chip.setPadding(dp(4), dp(6), dp(4), dp(6));
            chip.setBackground(rounded(color("row_bg"), 8, 0));
            TextView value = text(s.optString("value"), "title_sp", "text", true);
            value.setGravity(Gravity.CENTER);
            TextView label = text(s.optString("label"), "small_sp", "text_dim", false);
            label.setGravity(Gravity.CENTER);
            chip.addView(value);
            chip.addView(label);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            if (i > 0) lp.leftMargin = dp(6);
            chips.addView(chip, lp);
        }
        addRow(chips, 8);

        LinearLayout nextRow = new LinearLayout(activity);
        nextRow.setGravity(Gravity.CENTER_VERTICAL);
        nextRow.addView(line(c.optString("next"), "text_sp", "accent", true),
                new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        if (c.optBoolean("hardmode")) {
            TextView hm = text("Hardmode", "small_sp", "panel_bg", true);
            hm.setPadding(dp(8), dp(2), dp(8), dp(2));
            hm.setBackground(rounded(color("error"), 10, 0));
            nextRow.addView(hm);
        }
        addRow(nextRow, 8);

        JSONArray rows = c.optJSONArray("rows");
        for (int i = 0; rows != null && i < rows.length(); i++) {
            JSONObject r = rows.optJSONObject(i);
            if (r.optBoolean("header")) {
                TextView h = text(r.optString("name").toUpperCase(), "small_sp", "text_dim", true);
                h.setPadding(dp(4), dp(8), 0, dp(4));
                content.addView(h);
                continue;
            }
            LinearLayout row = new LinearLayout(activity);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(10), dp(6), dp(8), dp(6));
            row.setBackground(rounded(color("row_bg"), 8, 0));
            LinearLayout texts = new LinearLayout(activity);
            texts.setOrientation(LinearLayout.VERTICAL);
            texts.addView(line(r.optString("name"), "text_sp", "text", false));
            texts.addView(line(r.optString("category"), "small_sp", "text_dim", false));
            row.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            String status = r.optString("status");
            TextView pill = text(r.optString("status_label"), "small_sp", "panel_bg", true);
            pill.setPadding(dp(8), dp(2), dp(8), dp(2));
            pill.setBackground(rounded(color(status), 10, 0));
            row.addView(pill);
            addRow(row, 4);
        }
    }

    // ------------------------------------------------------------------------
    private void addRow(View v, int bottomDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(bottomDp);
        content.addView(v, lp);
    }

    private View toggle(boolean on) {
        FrameLayout track = new FrameLayout(activity);
        track.setBackground(rounded(color(on ? "switch_on" : "switch_off"), 11, 0));
        View knob = new View(activity);
        GradientDrawable k = new GradientDrawable();
        k.setShape(GradientDrawable.OVAL);
        k.setColor(color("text"));
        knob.setBackground(k);
        FrameLayout.LayoutParams kl = new FrameLayout.LayoutParams(dp(18), dp(18),
                Gravity.CENTER_VERTICAL | (on ? Gravity.END : Gravity.START));
        kl.leftMargin = kl.rightMargin = dp(2);
        track.addView(knob, kl);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(40), dp(22));
        lp.leftMargin = dp(8);
        track.setLayoutParams(lp);
        return track;
    }

    private TextView text(String s, String sizeKey, String colorKey, boolean bold) {
        TextView t = new TextView(activity);
        t.setText(s);  // UTF-8 -> UTF-16 já feito no JNI: acentos renderizam nativamente
        t.setTextColor(color(colorKey));
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, theme.optInt(sizeKey, 14));
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    /** Texto de uma linha só, com reticências: linhas de lista não quebram nem empurram o interruptor. */
    private TextView line(String s, String sizeKey, String colorKey, boolean bold) {
        TextView t = text(s, sizeKey, colorKey, bold);
        t.setSingleLine(true);
        t.setEllipsize(TextUtils.TruncateAt.END);
        return t;
    }

    private GradientDrawable rounded(int fill, int radiusDp, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radiusDp));
        if (stroke != 0) d.setStroke(dp(1), stroke);
        return d;
    }

    private int color(String key) {
        try {
            return Color.parseColor(theme.optString(key, "#FFFF00FF"));
        } catch (IllegalArgumentException e) {
            return Color.MAGENTA;
        }
    }

    private int dp(float v) {
        return Math.round(v * density);
    }
}
