package com.tlmod.overlay;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import android.text.InputType;
import android.text.style.ForegroundColorSpan;
import android.view.KeyEvent;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
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

import java.util.HashMap;
import java.util.Map;

/**
 * Renderizador do painel do TLMod: Views Android presas à janela da Activity
 * do jogo (sem permissão de sobreposição).
 *
 * Não decide nada: lê o view-model (JSON) que o PanelController em C++ produz
 * e devolve os toques como ações. Todo texto, cor e medida vem do JSON.
 *
 * Toque: o FrameLayout raiz não é clicável, então toques fora do botão e do
 * painel seguem para a View do Unity, que está embaixo. O painel e o botão
 * consomem os toques que caem neles, e o jogo não os recebe.
 */
public final class TLModOverlay {
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
    private ScrollView scroll;
    private LinearLayout content;
    private LinearLayout tabsRow;
    private String renderedTab = "";
    // Campos de busca das abas de mod, reaproveitados entre desenhos para não
    // perder o texto digitado nem o foco (chave: "<mod>/<id da busca>").
    private final Map<String, EditText> searchFields = new HashMap<>();
    private long renderedVersion = -1;
    private JSONObject theme;

    private TLModOverlay(Activity activity, long handle) {
        this.activity = activity;
        this.handle = handle;
        this.density = activity.getResources().getDisplayMetrics().density;
    }

    /** Chamado pelo C++ (qualquer thread): monta o overlay na thread de UI. */
    public static void install(final Activity activity, final long handle) {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                new TLModOverlay(activity, handle).attach();
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
            // Enquanto um campo de busca está sendo editado, a aba não é redesenhada
            // (recriar as Views fecharia o teclado); o desenho alcança depois.
            if (nativeTick(handle, SystemClock.uptimeMillis()) && !editingSearch()) {
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

    private boolean editingSearch() {
        for (EditText e : searchFields.values())
            if (e.hasFocus()) return true;
        return false;
    }

    private void hideKeyboard() {
        for (EditText e : searchFields.values()) {
            if (!e.hasFocus()) continue;
            InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(e.getWindowToken(), 0);
            e.clearFocus();
        }
    }

    private void act(String action, String arg) {
        hideKeyboard();
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
        TextView title = text("TLMod", "title_sp", "accent", true);
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

        scroll = new ScrollView(activity);
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
            tab.setSingleLine(true);
            // Largura do texto + parte igual da sobra: "Boss Checklist" não quebra ao lado de "Log".
            tabsRow.addView(tab, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }

        // Na aba Log, acompanha o fim (linhas novas) se já estava no fim ou se acabou de entrar nela.
        String tabId = s.optString("tab");
        boolean follow = "log".equals(tabId) && (!tabId.equals(renderedTab) || atBottom());
        renderedTab = tabId;
        content.removeAllViews();
        if ("mods".equals(tabId)) renderMods(s.optJSONObject("mods"));
        else if ("log".equals(tabId)) renderLog(s.optJSONObject("log"));
        else if (tabId.startsWith("mod:")) renderModTab(s.optJSONObject("modtab"));
        else renderChecklist(s.optJSONObject("checklist"));
        if (follow) {
            scroll.post(new Runnable() {
                @Override public void run() { scroll.fullScroll(View.FOCUS_DOWN); }
            });
        }
    }

    private boolean atBottom() {
        View child = scroll.getChildAt(0);
        return child == null || child.getBottom() - (scroll.getHeight() + scroll.getScrollY()) <= dp(24);
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
            if (!TextUtils.isEmpty(r.optString("detail"))) {
                // Erro inteiro: quebra linha, nunca reticências (a causa costuma estar no fim do
                // dlerror). Selecionável para copiar.
                TextView detail = text(r.optString("detail"), "small_sp", "error", false);
                detail.setTextIsSelectable(true);
                detail.setPadding(0, dp(2), 0, dp(2));
                texts.addView(detail);
            }
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

    // ------------------------------------------------------------------------
    // Aba de um mod: seções com nota, campo de busca e linhas de botões, tudo
    // descrito pelo JSON do mod (ver panel.h, struct ModTab).
    private void renderModTab(JSONObject tab) {
        if (tab == null) return;
        String notice = tab.optString("notice");
        if (!TextUtils.isEmpty(notice)) {
            TextView n = text(notice, "small_sp", "accent", true);
            n.setPadding(dp(4), 0, dp(4), dp(8));
            content.addView(n);
        }
        JSONObject c = tab.optJSONObject("content");
        JSONArray sections = c != null ? c.optJSONArray("sections") : null;
        final String modId = tab.optString("id");
        for (int i = 0; sections != null && i < sections.length(); i++) {
            JSONObject sec = sections.optJSONObject(i);
            if (sec == null) continue;
            TextView h = text(sec.optString("title").toUpperCase(), "small_sp", "text_dim", true);
            h.setPadding(dp(4), dp(i == 0 ? 0 : 10), 0, dp(4));
            content.addView(h);
            JSONObject search = sec.optJSONObject("search");
            if (search != null) addRow(searchRow(modId, search), 4);
            String note = sec.optString("note");
            if (!TextUtils.isEmpty(note)) {
                TextView nt = text(note, "small_sp", "text_dim", false);
                nt.setPadding(dp(4), 0, dp(4), dp(4));
                content.addView(nt);
            }
            JSONArray rows = sec.optJSONArray("rows");
            for (int r = 0; rows != null && r < rows.length(); r++) {
                JSONObject row = rows.optJSONObject(r);
                if (row != null) addRow(modRow(row), 4);
            }
        }
    }

    private View modRow(JSONObject row) {
        String title = row.optString("text");
        String detail = row.optString("detail");
        JSONArray buttons = row.optJSONArray("buttons");
        int nButtons = buttons != null ? buttons.length() : 0;
        LinearLayout box = new LinearLayout(activity);
        box.setPadding(dp(10), dp(6), dp(8), dp(6));
        box.setBackground(rounded(color("row_bg"), 8, 0));
        LinearLayout texts = new LinearLayout(activity);
        texts.setOrientation(LinearLayout.VERTICAL);
        if (!TextUtils.isEmpty(title)) texts.addView(line(title, "text_sp", "text", false));
        if (!TextUtils.isEmpty(detail)) texts.addView(line(detail, "small_sp", "text_dim", false));
        boolean hasText = texts.getChildCount() > 0;
        if (hasText && nButtons <= 3) {  // texto à esquerda, poucos botões na mesma linha
            box.setGravity(Gravity.CENTER_VERTICAL);
            box.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            for (int b = 0; b < nButtons; b++) box.addView(chip(buttons.optJSONObject(b)), chipParams());
            return box;
        }
        box.setOrientation(LinearLayout.VERTICAL);  // muitos botões: quebram em linhas
        if (hasText) box.addView(texts);
        int avail = dp(theme.optInt("panel_width_dp", 400)) - dp(10) * 2 - dp(18);
        LinearLayout lineView = null;
        int used = 0;
        Paint paint = new Paint();
        paint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, theme.optInt("small_sp", 12),
                activity.getResources().getDisplayMetrics()));
        for (int b = 0; b < nButtons; b++) {
            JSONObject spec = buttons.optJSONObject(b);
            int w = (int) paint.measureText(spec != null ? spec.optString("label") : "") + dp(20) + dp(6);
            if (lineView == null || used + w > avail) {
                lineView = new LinearLayout(activity);
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.topMargin = dp(4);
                box.addView(lineView, lp);
                used = 0;
            }
            lineView.addView(chip(spec), chipParams());
            used += w;
        }
        return box;
    }

    private LinearLayout.LayoutParams chipParams() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = dp(6);
        return lp;
    }

    /** Botão do mod: toque manda o texto de "command"; "active" = ligado (cor de destaque). */
    private TextView chip(final JSONObject spec) {
        boolean active = spec != null && spec.optBoolean("active");
        TextView b = text(spec != null ? spec.optString("label") : "?", "small_sp", active ? "panel_bg" : "text", true);
        b.setSingleLine(true);
        b.setPadding(dp(10), dp(5), dp(10), dp(5));
        b.setBackground(rounded(color(active ? "switch_on" : "tab_active_bg"), 12, 0));
        if (spec != null) {
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { act("mod_command", spec.optString("command")); }
            });
        }
        return b;
    }

    /** Campo de busca + botão Buscar. Manda "search <id> <texto>" ao mod. */
    private View searchRow(String modId, JSONObject spec) {
        final String id = spec.optString("id");
        String key = modId + "/" + id;
        EditText field = searchFields.get(key);
        if (field == null) {
            field = new EditText(activity);
            field.setSingleLine(true);
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            field.setImeOptions(EditorInfo.IME_ACTION_SEARCH | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            field.setTextColor(color("text"));
            field.setHintTextColor(color("text_dim"));
            field.setTextSize(TypedValue.COMPLEX_UNIT_SP, theme.optInt("text_sp", 14));
            field.setBackground(rounded(color("row_bg"), 8, color("panel_border")));
            field.setPadding(dp(10), dp(6), dp(10), dp(6));
            field.setText(spec.optString("query"));
            final EditText f = field;
            field.setOnEditorActionListener(new TextView.OnEditorActionListener() {
                @Override public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
                    act("mod_command", "search " + id + " " + f.getText().toString().trim());
                    return true;
                }
            });
            searchFields.put(key, field);
        } else if (field.getParent() != null) {
            ((ViewGroup) field.getParent()).removeView(field);
        }
        field.setHint(spec.optString("placeholder"));
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(field, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        final EditText f = field;
        TextView go = text("Buscar", "small_sp", "text", true);
        go.setPadding(dp(12), dp(8), dp(12), dp(8));
        go.setBackground(rounded(color("tab_active_bg"), 8, 0));
        go.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                act("mod_command", "search " + id + " " + f.getText().toString().trim());
            }
        });
        row.addView(go, chipParams());
        return row;
    }

    private void renderLog(JSONObject log) {
        if (log == null) return;
        String file = log.optString("file");
        TextView where = text(TextUtils.isEmpty(file) ? "Sem arquivo de log" : file, "small_sp", "text_dim", false);
        where.setTextIsSelectable(true);
        where.setPadding(dp(4), 0, dp(4), dp(6));
        content.addView(where);
        JSONArray lines = log.optJSONArray("lines");
        if (lines == null || lines.length() == 0) {
            TextView msg = text(log.optString("empty"), "text_sp", "text_dim", false);
            msg.setGravity(Gravity.CENTER);
            msg.setPadding(dp(8), dp(48), dp(8), dp(48));
            content.addView(msg);
            return;
        }
        // Um TextView só, com cor por nível: rápido de redesenhar e dá para selecionar e copiar.
        SpannableStringBuilder sb = new SpannableStringBuilder();
        for (int i = 0; i < lines.length(); i++) {
            JSONObject l = lines.optJSONObject(i);
            String level = l.optString("level");
            int start = sb.length();
            sb.append(l.optString("text"));
            String key = "E".equals(level) ? "error" : ("W".equals(level) ? "pending" : "text");
            sb.setSpan(new ForegroundColorSpan(color(key)), start, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            if (i + 1 < lines.length()) sb.append('\n');
        }
        TextView body = text("", "small_sp", "text", false);
        body.setTypeface(Typeface.MONOSPACE);
        body.setText(sb);
        body.setTextIsSelectable(true);
        body.setPadding(dp(8), dp(6), dp(8), dp(6));
        body.setBackground(rounded(color("row_bg"), 8, 0));
        addRow(body, 0);
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
