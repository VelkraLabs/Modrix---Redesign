package com.tlmod.overlay;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
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
import android.view.animation.DecelerateInterpolator;
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
 * Renderizador do overlay do TLMod: Views Android presas à janela da Activity
 * do jogo (sem permissão de sobreposição).
 *
 * Superfícies, uma aberta por vez (o C++ decide qual): o painel do botão "M"
 * (Log), a gaveta do Boss Checklist (desliza da esquerda) e o modal de um mod
 * (ex.: Ferramentas, no centro). Os ícones da gaveta e dos modais ficam numa
 * coluna abaixo do "M" e só aparecem com um mundo carregado.
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
    private LinearLayout panel;  // o "M": Log
    private int panelMaxH;  // altura máxima; o painel encolhe para caber no conteúdo
    private TextView panelTitle;
    private ScrollView scroll;
    private LinearLayout panelContent;
    // Para onde os render*() desenham, e a largura útil (quebra dos botões dos mods).
    private LinearLayout content;
    private int contentWidthPx;
    private String renderedSurface = "none";
    // Ícones abaixo do "M".
    private LinearLayout launcherColumn;
    private String renderedLaunchers = "";
    // Gaveta do Boss Checklist: fundo escurecido + cartão preso à esquerda.
    private FrameLayout drawerLayer;
    private View drawerScrim;
    private LinearLayout drawer;
    private TextView drawerTitle;
    private LinearLayout drawerContent;
    private boolean drawerShown;
    // Modal de um mod: fundo escurecido + cartão no centro.
    private FrameLayout modalLayer;
    private LinearLayout modalCard;
    private int modalMaxH;
    private TextView modalTitle;
    private LinearLayout modalContent;
    private boolean modalShown;
    // Campos de busca das abas de mod, reaproveitados entre desenhos para não
    // perder o texto digitado nem o foco (chave: "<mod>/<id da busca>").
    private final Map<String, EditText> searchFields = new HashMap<>();
    private long renderedVersion = -1;
    private JSONObject theme;
    // Oficina de Mods: tela grande por cima do jogo (aberta pelo botão "Mods" do menu).
    private FrameLayout workshop;
    private TextView wsTitle, wsSummary, wsNotice, wsFooter, wsClose;
    private ScrollView wsScroll;
    private LinearLayout wsContent;

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
        buildLaunchers();
        buildPanel();
        buildDrawer();
        buildModal();
        buildWorkshop();
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
        panelTitle = text("TLMod", "title_sp", "accent", true);
        header.addView(panelTitle, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView close = text("✕", "title_sp", "text_dim", false);
        close.setPadding(dp(10), dp(2), dp(4), dp(2));
        close.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("close", ""); }
        });
        header.addView(close);
        panel.addView(header);

        scroll = new ScrollView(activity);
        panelContent = new LinearLayout(activity);
        panelContent.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(panelContent);
        LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        sl.topMargin = dp(8);
        panel.addView(scroll, sl);

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
    // Ícones abaixo do "M": um por superfície (gaveta do checklist, modal de cada mod).
    private void buildLaunchers() {
        int margin = dp(theme.optInt("margin_dp", 12));
        int fabSize = dp(theme.optInt("fab_size_dp", 44));
        int size = dp(theme.optInt("launcher_size_dp", 40));
        launcherColumn = new LinearLayout(activity);
        launcherColumn.setOrientation(LinearLayout.VERTICAL);
        launcherColumn.setVisibility(View.GONE);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START);
        lp.leftMargin = margin + (fabSize - size) / 2;  // centrados sob a posição inicial do "M"
        lp.topMargin = margin * 6 + fabSize + dp(14);
        root.addView(launcherColumn, lp);
    }

    private void renderLaunchers(JSONObject s) {
        JSONArray list = s.optJSONArray("launchers");
        boolean visible = s.optBoolean("launchers_visible") && list != null && list.length() > 0;
        launcherColumn.setVisibility(visible ? View.VISIBLE : View.GONE);
        String key = list != null ? list.toString() : "";
        if (key.equals(renderedLaunchers)) return;
        renderedLaunchers = key;
        launcherColumn.removeAllViews();
        int size = dp(theme.optInt("launcher_size_dp", 40));
        for (int i = 0; list != null && i < list.length(); i++) {
            final JSONObject l = list.optJSONObject(i);
            if (l == null) continue;
            LauncherView v = new LauncherView(activity, l.optString("icon"), l.optString("letter"), l.optBoolean("active"));
            v.setContentDescription(l.optString("label"));
            v.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View view) { act("open", l.optString("id")); }
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(size, size);
            if (i > 0) lp.topMargin = dp(10);
            launcherColumn.addView(v, lp);
        }
    }

    private String launcherLabel(JSONObject s, String id) {
        JSONArray list = s.optJSONArray("launchers");
        for (int i = 0; list != null && i < list.length(); i++) {
            JSONObject l = list.optJSONObject(i);
            if (l != null && id.equals(l.optString("id"))) return l.optString("label");
        }
        return "";
    }

    /** Ícone redondo desenhado à mão (Canvas): cor de destaque quando a superfície dele está aberta. */
    private final class LauncherView extends View {
        private final String icon, letter;
        private final boolean active;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();

        LauncherView(Context c, String icon, String letter, boolean active) {
            super(c);
            this.icon = icon;
            this.letter = letter;
            this.active = active;
            setClickable(true);
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight(), cx = w / 2f, cy = h / 2f, r = Math.min(w, h) / 2f - dp(1);
            int fg = active ? color("panel_bg") : color("accent");
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(active ? color("accent") : color("fab_bg"));
            cv.drawCircle(cx, cy, r, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(2));
            paint.setColor(color("accent"));
            cv.drawCircle(cx, cy, r, paint);

            float u = r / 10f;  // unidade do desenho: o círculo tem raio 10
            paint.setColor(fg);
            paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setStrokeJoin(Paint.Join.ROUND);
            paint.setStrokeWidth(1.4f * u);
            path.reset();
            if ("checklist".equals(icon)) {
                // Prancheta com duas marcas de "feito".
                paint.setStyle(Paint.Style.STROKE);
                cv.drawRoundRect(new RectF(cx - 4.5f * u, cy - 5f * u, cx + 4.5f * u, cy + 6f * u), u, u, paint);
                paint.setStyle(Paint.Style.FILL);
                cv.drawRoundRect(new RectF(cx - 2.2f * u, cy - 6.5f * u, cx + 2.2f * u, cy - 4f * u), u * 0.6f, u * 0.6f, paint);
                paint.setStyle(Paint.Style.STROKE);
                for (float y : new float[] {-1.2f, 2.8f}) {
                    path.moveTo(cx - 3f * u, cy + y * u);
                    path.lineTo(cx - 2f * u, cy + (y + 1f) * u);
                    path.lineTo(cx - 0.4f * u, cy + (y - 1f) * u);
                    path.moveTo(cx + 0.8f * u, cy + y * u);
                    path.lineTo(cx + 3f * u, cy + y * u);
                }
                cv.drawPath(path, paint);
            } else if ("tools".equals(icon)) {
                // Chave inglesa na diagonal: cabo e cabeça com a boca aberta para cima à direita.
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(2.2f * u);
                cv.drawLine(cx - 4.5f * u, cy + 4.5f * u, cx + 1.2f * u, cy - 1.2f * u, paint);
                float hx = cx + 3f * u, hy = cy - 3f * u;
                paint.setStyle(Paint.Style.FILL);
                cv.drawCircle(hx, hy, 3.3f * u, paint);
                paint.setColor(active ? color("accent") : color("fab_bg"));  // a boca: um recorte na cor do fundo
                cv.drawCircle(hx + 1.6f * u, hy - 1.6f * u, 1.7f * u, paint);
                cv.drawRect(hx + 0.6f * u, hy - 5f * u, hx + 5f * u, hy - 0.6f * u, paint);
            } else if ("star".equals(icon) || "gem".equals(icon)) {
                paint.setStyle(Paint.Style.FILL);
                int points = "star".equals(icon) ? 5 : 4;
                for (int k = 0; k < points * 2; k++) {
                    double a = Math.PI / points * k - Math.PI / 2;
                    float rr = (k % 2 == 0 ? 6f : ("star".equals(icon) ? 2.6f : 3.6f)) * u;
                    float px = cx + (float) Math.cos(a) * rr, py = cy + (float) Math.sin(a) * rr;
                    if (k == 0) path.moveTo(px, py); else path.lineTo(px, py);
                }
                path.close();
                cv.drawPath(path, paint);
            } else if ("sword".equals(icon)) {
                paint.setStyle(Paint.Style.STROKE);
                cv.drawLine(cx - 4f * u, cy + 4f * u, cx + 5f * u, cy - 5f * u, paint);
                cv.drawLine(cx - 4.5f * u, cy + 0.5f * u, cx - 0.5f * u, cy + 4.5f * u, paint);
            } else if ("book".equals(icon)) {
                paint.setStyle(Paint.Style.STROKE);
                cv.drawRoundRect(new RectF(cx - 4.5f * u, cy - 5.5f * u, cx + 4.5f * u, cy + 5.5f * u), u, u, paint);
                cv.drawLine(cx - 2.5f * u, cy - 5.5f * u, cx - 2.5f * u, cy + 5.5f * u, paint);
            } else {
                paint.setStyle(Paint.Style.FILL);
                paint.setTextAlign(Paint.Align.CENTER);
                paint.setTypeface(Typeface.DEFAULT_BOLD);
                paint.setTextSize(9f * u);
                Paint.FontMetrics fm = paint.getFontMetrics();
                cv.drawText(TextUtils.isEmpty(letter) ? "?" : letter, cx, cy - (fm.ascent + fm.descent) / 2f, paint);
            }
        }
    }

    // ------------------------------------------------------------------------
    // Gaveta do Boss Checklist: entra da esquerda; toque no fundo ou no X fecha.
    private void buildDrawer() {
        drawerLayer = new FrameLayout(activity);
        drawerLayer.setVisibility(View.GONE);
        root.addView(drawerLayer, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        drawerScrim = new View(activity);
        drawerScrim.setBackgroundColor(color("scrim"));
        drawerScrim.setClickable(true);
        drawerScrim.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("close", ""); }
        });
        drawerLayer.addView(drawerScrim, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        drawer = new LinearLayout(activity);
        drawer.setOrientation(LinearLayout.VERTICAL);
        drawer.setClickable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(color("panel_bg"));
        float rad = dp(theme.optInt("corner_dp", 12));
        bg.setCornerRadii(new float[] {0, 0, rad, rad, rad, rad, 0, 0});  // cantos arredondados só à direita
        bg.setStroke(dp(1), color("panel_border"));
        drawer.setBackground(bg);
        int pad = dp(12);
        drawer.setPadding(pad, pad, pad, pad);
        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        drawerTitle = text("", "title_sp", "accent", true);
        header.addView(drawerTitle, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        header.addView(closeButton());
        drawer.addView(header);
        ScrollView ds = new ScrollView(activity);
        drawerContent = new LinearLayout(activity);
        drawerContent.setOrientation(LinearLayout.VERTICAL);
        ds.addView(drawerContent);
        LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        sl.topMargin = dp(8);
        drawer.addView(ds, sl);
        drawerLayer.addView(drawer, new FrameLayout.LayoutParams(drawerWidth(), ViewGroup.LayoutParams.MATCH_PARENT,
                Gravity.TOP | Gravity.START));
    }

    private int drawerWidth() {
        int screenW = activity.getResources().getDisplayMetrics().widthPixels;
        return Math.min(dp(theme.optInt("drawer_width_dp", 360)), (int) (screenW * 0.7f));
    }

    private void showDrawer(boolean show) {
        if (show == drawerShown) return;
        drawerShown = show;
        drawer.animate().cancel();
        drawerScrim.animate().cancel();
        float w = drawer.getLayoutParams().width;
        if (show) {
            drawerLayer.setVisibility(View.VISIBLE);
            drawer.setTranslationX(-w);
            drawerScrim.setAlpha(0f);
            drawer.animate().translationX(0f).setDuration(220).setInterpolator(new DecelerateInterpolator()).start();
            drawerScrim.animate().alpha(1f).setDuration(220).start();
        } else {
            drawerScrim.animate().alpha(0f).setDuration(180).start();
            drawer.animate().translationX(-w).setDuration(180).setInterpolator(new DecelerateInterpolator())
                    .withEndAction(new Runnable() {
                        @Override public void run() { if (!drawerShown) drawerLayer.setVisibility(View.GONE); }
                    }).start();
        }
    }

    // ------------------------------------------------------------------------
    // Modal de um mod: cartão no centro, por cima de um fundo escurecido.
    private void buildModal() {
        modalLayer = new FrameLayout(activity);
        modalLayer.setVisibility(View.GONE);
        modalLayer.setBackgroundColor(color("scrim"));
        modalLayer.setClickable(true);  // toque fora do cartão fecha (e não vai para o jogo)
        modalLayer.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("close", ""); }
        });
        root.addView(modalLayer, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        modalCard = new LinearLayout(activity) {
            @Override protected void onMeasure(int widthSpec, int heightSpec) {
                super.onMeasure(widthSpec, MeasureSpec.makeMeasureSpec(modalMaxH, MeasureSpec.AT_MOST));
            }
        };
        modalCard.setOrientation(LinearLayout.VERTICAL);
        modalCard.setClickable(true);
        modalCard.setBackground(rounded(color("panel_bg"), theme.optInt("corner_dp", 12), color("panel_border")));
        int pad = dp(12);
        modalCard.setPadding(pad, pad, pad, pad);
        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        modalTitle = text("", "title_sp", "accent", true);
        header.addView(modalTitle, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        header.addView(closeButton());
        modalCard.addView(header);
        ScrollView ms = new ScrollView(activity);
        modalContent = new LinearLayout(activity);
        modalContent.setOrientation(LinearLayout.VERTICAL);
        ms.addView(modalContent);
        LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        sl.topMargin = dp(8);
        modalCard.addView(ms, sl);
        int margin = dp(theme.optInt("margin_dp", 12));
        int screenW = activity.getResources().getDisplayMetrics().widthPixels;
        int screenH = activity.getResources().getDisplayMetrics().heightPixels;
        modalMaxH = screenH - 2 * margin;
        modalLayer.addView(modalCard, new FrameLayout.LayoutParams(
                Math.min(dp(theme.optInt("modal_width_dp", 460)), screenW - 2 * margin),
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER));
    }

    private void showModal(boolean show) {
        if (show == modalShown) return;
        modalShown = show;
        modalLayer.animate().cancel();
        if (show) {
            modalLayer.setVisibility(View.VISIBLE);
            modalLayer.setAlpha(0f);
            modalCard.setScaleX(0.96f);
            modalCard.setScaleY(0.96f);
            modalLayer.animate().alpha(1f).setDuration(150).start();
            modalCard.animate().scaleX(1f).scaleY(1f).setDuration(150).start();
        } else {
            modalLayer.animate().alpha(0f).setDuration(120).withEndAction(new Runnable() {
                @Override public void run() { if (!modalShown) modalLayer.setVisibility(View.GONE); }
            }).start();
        }
    }

    private TextView closeButton() {
        TextView close = text("✕", "title_sp", "text_dim", false);
        close.setPadding(dp(10), dp(2), dp(4), dp(2));
        close.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("close", ""); }
        });
        return close;
    }

    // ------------------------------------------------------------------------
    private void render(JSONObject s) {
        long version = s.optLong("version");
        if (version == renderedVersion) return;
        renderedVersion = version;

        JSONObject fabState = s.optJSONObject("fab");
        fab.setText(fabState != null ? fabState.optString("label", "M") : "M");
        fab.setVisibility(fabState == null || fabState.optBoolean("visible", true) ? View.VISIBLE : View.GONE);
        renderLaunchers(s);
        JSONObject ws = s.optJSONObject("workshop");
        boolean wsOpen = ws != null && ws.optBoolean("open");
        workshop.setVisibility(wsOpen ? View.VISIBLE : View.GONE);
        if (wsOpen) renderWorkshop(ws);

        String surface = s.optString("surface", "none");
        boolean entered = !surface.equals(renderedSurface);
        renderedSurface = surface;

        // "M": o Log. Acompanha o fim (linhas novas) se já estava no fim ou se acabou de abrir.
        boolean logOpen = "log".equals(surface);
        panel.setVisibility(logOpen ? View.VISIBLE : View.GONE);
        if (logOpen) {
            panelTitle.setText(s.optString("panel_title", "TLMod"));
            boolean follow = entered || atBottom();
            content = panelContent;
            contentWidthPx = dp(theme.optInt("panel_width_dp", 400)) - dp(20);
            content.removeAllViews();
            renderLog(s.optJSONObject("log"));
            if (follow) {
                scroll.post(new Runnable() {
                    @Override public void run() { scroll.fullScroll(View.FOCUS_DOWN); }
                });
            }
        }

        boolean checklistOpen = "checklist".equals(surface);
        showDrawer(checklistOpen);
        if (checklistOpen) {
            drawerTitle.setText(launcherLabel(s, "checklist"));
            content = drawerContent;
            contentWidthPx = drawer.getLayoutParams().width - dp(24);
            content.removeAllViews();
            renderChecklist(s.optJSONObject("checklist"));
        }

        boolean modOpen = surface.startsWith("mod:");
        showModal(modOpen);
        if (modOpen) {
            JSONObject tab = s.optJSONObject("modtab");
            modalTitle.setText(tab != null ? tab.optString("title") : "");
            content = modalContent;
            contentWidthPx = modalCard.getLayoutParams().width - dp(24);
            content.removeAllViews();
            renderModTab(tab);
        }
    }

    private boolean atBottom() {
        View child = scroll.getChildAt(0);
        return child == null || child.getBottom() - (scroll.getHeight() + scroll.getScrollY()) <= dp(24);
    }

    // ------------------------------------------------------------------------
    // Oficina de Mods: fundo escurecido (consome os toques: o menu do jogo fica
    // inerte embaixo) e um cartão com cabeçalho, seções e rodapé.
    private void buildWorkshop() {
        workshop = new FrameLayout(activity);
        workshop.setBackgroundColor(0xB3000000);
        workshop.setClickable(true);
        workshop.setVisibility(View.GONE);
        root.addView(workshop, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout card = new LinearLayout(activity);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setClickable(true);
        card.setBackground(rounded(color("panel_bg"), theme.optInt("corner_dp", 12), color("panel_border")));
        int pad = dp(14);
        card.setPadding(pad, pad, pad, pad);

        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout titles = new LinearLayout(activity);
        titles.setOrientation(LinearLayout.VERTICAL);
        wsTitle = text("", "title_sp", "accent", true);
        wsTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, theme.optInt("title_sp", 16) + 4);
        titles.addView(wsTitle);
        wsSummary = text("", "small_sp", "text_dim", false);
        titles.addView(wsSummary);
        header.addView(titles, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        wsClose = text("", "text_sp", "text", true);
        wsClose.setPadding(dp(14), dp(8), dp(14), dp(8));
        wsClose.setBackground(rounded(color("tab_active_bg"), 8, 0));
        wsClose.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("workshop_close", ""); }
        });
        header.addView(wsClose);
        card.addView(header);

        wsNotice = text("", "small_sp", "accent", true);
        wsNotice.setPadding(0, dp(8), 0, 0);
        card.addView(wsNotice);

        wsScroll = new ScrollView(activity);
        wsContent = new LinearLayout(activity);
        wsContent.setOrientation(LinearLayout.VERTICAL);
        wsScroll.addView(wsContent);
        LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        sl.topMargin = dp(8);
        card.addView(wsScroll, sl);

        wsFooter = text("", "small_sp", "text_dim", false);
        wsFooter.setPadding(0, dp(8), 0, 0);
        card.addView(wsFooter);

        int margin = dp(theme.optInt("margin_dp", 12));
        int screenW = activity.getResources().getDisplayMetrics().widthPixels;
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                Math.min(screenW - 2 * margin, dp(720)), ViewGroup.LayoutParams.MATCH_PARENT, Gravity.CENTER);
        lp.topMargin = lp.bottomMargin = margin;
        workshop.addView(card, lp);
    }

    private void renderWorkshop(JSONObject ws) {
        wsTitle.setText(ws.optString("title"));
        wsSummary.setText(ws.optString("summary"));
        wsClose.setText(ws.optString("close_label", "Fechar"));
        String notice = ws.optString("notice");
        wsNotice.setText(notice);
        wsNotice.setVisibility(TextUtils.isEmpty(notice) ? View.GONE : View.VISIBLE);
        wsFooter.setText(ws.optString("footer"));
        wsContent.removeAllViews();
        String empty = ws.optString("empty");
        if (!TextUtils.isEmpty(empty)) {
            TextView e = text(empty, "text_sp", "text_dim", false);
            e.setPadding(0, dp(12), 0, dp(12));
            wsContent.addView(e);
        }
        JSONArray sections = ws.optJSONArray("sections");
        for (int i = 0; sections != null && i < sections.length(); i++) {
            JSONObject sec = sections.optJSONObject(i);
            TextView h = text(sec.optString("title"), "text_sp", "accent", true);
            h.setPadding(dp(2), dp(i == 0 ? 2 : 12), 0, dp(6));
            wsContent.addView(h);
            JSONArray rows = sec.optJSONArray("rows");
            for (int k = 0; rows != null && k < rows.length(); k++) wsContent.addView(workshopRow(rows.optJSONObject(k)));
        }
    }

    private View workshopRow(final JSONObject r) {
        LinearLayout card = new LinearLayout(activity);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(12), dp(10), dp(12), dp(10));
        card.setBackground(rounded(color("row_bg"), 10, 0));
        LinearLayout.LayoutParams cl = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        cl.bottomMargin = dp(8);
        card.setLayoutParams(cl);

        LinearLayout top = new LinearLayout(activity);
        top.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout names = new LinearLayout(activity);
        names.setOrientation(LinearLayout.VERTICAL);
        names.addView(line(r.optString("name"), "text_sp", "text", true));
        String by = r.optString("version");
        if (!TextUtils.isEmpty(r.optString("authors")))
            by = TextUtils.isEmpty(by) ? r.optString("authors") : by + " · " + r.optString("authors");
        if (!TextUtils.isEmpty(by)) names.addView(line(by, "small_sp", "text_dim", false));
        top.addView(names, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        String state = r.optString("state");
        String pillColor = "loaded".equals(state) ? "defeated" : ("disabled".equals(state) ? "unknown" : "error");
        TextView pill = text(r.optString("state_label"), "small_sp", "panel_bg", true);
        pill.setPadding(dp(8), dp(2), dp(8), dp(2));
        pill.setBackground(rounded(color(pillColor), 10, 0));
        top.addView(pill);
        View sw = toggle(r.optBoolean("enabled"));
        sw.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { act("toggle_mod", r.optString("id")); }
        });
        top.addView(sw);
        card.addView(top);

        addIf(card, r.optString("description"), "small_sp", "text", 6);
        addIf(card, r.optString("content"), "small_sp", "text_dim", 4);
        addIf(card, r.optString("pending"), "small_sp", "accent", 4);
        String toggleErr = r.optString("error_toggle");
        if (!TextUtils.isEmpty(toggleErr)) {
            TextView t = text(toggleErr, "small_sp", "error", true);
            t.setPaintFlags(t.getPaintFlags() | Paint.UNDERLINE_TEXT_FLAG);
            t.setPadding(0, dp(6), dp(12), dp(2));
            t.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { act("workshop_expand", r.optString("id")); }
            });
            card.addView(t);
        }
        String error = r.optString("error");
        if (!TextUtils.isEmpty(error)) {
            // Inteiro e selecionável (a causa costuma estar no fim do dlerror).
            TextView e = text(error, "small_sp", "error", false);
            e.setTextIsSelectable(true);
            e.setPadding(dp(8), dp(6), dp(8), dp(6));
            e.setBackground(rounded(color("panel_bg"), 6, 0));
            card.addView(e);
        }
        return card;
    }

    private void addIf(LinearLayout parent, String s, String sizeKey, String colorKey, int topDp) {
        if (TextUtils.isEmpty(s)) return;
        TextView t = text(s, sizeKey, colorKey, false);
        t.setPadding(0, dp(topDp), 0, 0);
        parent.addView(t);
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
        int avail = contentWidthPx - dp(10) * 2 - dp(18);
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
