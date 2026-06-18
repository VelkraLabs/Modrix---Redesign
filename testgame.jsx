/* ─────────────────────────────────────────────────────────────────────────
   Modrix — "Testar no jogo": overlay de boot simulado.
   Sem mundo — só a cerimônia de lançamento: compilar → NeoForge →
   registrar conteúdo → pronto, com barra de progresso e log.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useTgState, useEffect: useTgEffect, useRef: useTgRef } = React;

const TG_TIPS = [
  'Dica: clique com o botão direito numa célula da grade pra limpá-la.',
  'Dica: o código Java é regenerado a cada salvar — veja em “Ver Java”.',
  'Dica: receitas de fundição dão XP quando o jogador coleta o item.',
  'Dica: blocos com “Exige ferramenta” não dropam nada na mão.',
];

function tgPhases(mod) {
  const n = k => (mod[k] || []).length;
  return [
    { until: 22, label: 'Compilando mod…',            log: `> gradle runClient — build de ${mod.modId}-${mod.modVersion}` },
    { until: 48, label: 'Iniciando NeoForge…',        log: `  ✓ NeoForge ${mod.neoforge} · Java 21 · cliente 1.21` },
    { until: 78, label: 'Registrando conteúdo…',      log: `  ✓ ${n('items')} itens · ${n('blocks')} blocos · ${n('recipes')} receitas registrados` },
    { until: 100, label: 'Abrindo mundo de teste…',   log: '  ✓ mundo "modrix_test" carregado · sem erros' },
  ];
}

function TestGameOverlay({ mod, onClose, onLaunched }) {
  const [pct, setPct] = useTgState(0);
  const [log, setLog] = useTgState([]);
  const [done, setDone] = useTgState(false);
  const fired = useTgRef(false);
  const [tip] = useTgState(() => TG_TIPS[Math.floor(Math.random() * TG_TIPS.length)]);

  const phases = useTgRef(tgPhases(mod)).current;
  const phase = phases.find(p => pct < p.until) || phases[phases.length - 1];

  useTgEffect(() => {
    let cancelled = false;
    let v = 0;
    const logged = new Set();
    function tick() {
      if (cancelled) return;
      // avanço irregular, estilo loading de jogo
      v = Math.min(100, v + 2.2 + Math.random() * 4.2);
      setPct(Math.round(v));
      const ph = phases.find(p => Math.round(v) <= p.until) || phases[phases.length - 1];
      if (!logged.has(ph.log)) { logged.add(ph.log); setLog(l => [...l, ph.log]); }
      if (v >= 100) {
        setTimeout(() => {
          if (cancelled) return;
          setDone(true);
          if (!fired.current) { fired.current = true; onLaunched(); }
        }, 420);
        return;
      }
      setTimeout(tick, 70 + Math.random() * 90);
    }
    const t = setTimeout(tick, 380);
    return () => { cancelled = true; clearTimeout(t); };
  }, []);

  const n = k => (mod[k] || []).length;

  return (
    <div className="mx-veil" onClick={e => { if (e.target === e.currentTarget && done) onClose(); }}>
      <div className="mx-tg" data-screen-label="Overlay Testar no jogo">
        <button className="mx-exp-close" onClick={onClose} title={done ? 'Fechar' : 'Cancelar'}>✕</button>

        <div className={`mx-tg-emblem${done ? ' done' : ''}`}>
          <window.PixelSprite sprite={mod.emblem || 'sword'} size={64} />
        </div>

        {!done ? (
          <>
            <h2>Iniciando Minecraft</h2>
            <div className="mx-exp-sub">{mod.modName} · NeoForge {mod.neoforge} · mundo de teste</div>

            <div className="mx-tg-bar" role="progressbar" aria-valuenow={pct} aria-valuemin="0" aria-valuemax="100">
              <div className="mx-tg-fill" style={{ width: pct + '%' }} />
            </div>
            <div className="mx-tg-phase">
              <span>{phase.label}</span>
              <span className="mx-tg-pct">{pct}%</span>
            </div>

            <pre className="mx-exp-log mx-tg-log">{log.join('\n')}</pre>
            <div className="mx-tg-tip">{tip}</div>
          </>
        ) : (
          <>
            <h2>Mod carregado sem erros ✓</h2>
            <div className="mx-exp-sub">Minecraft aberto em outra janela · mundo "modrix_test"</div>

            <div className="mx-tg-stats">
              <span className="mx-tg-chip">{n('items')} itens</span>
              <span className="mx-tg-chip">{n('blocks')} blocos</span>
              <span className="mx-tg-chip">{n('recipes')} receitas</span>
              <span className="mx-tg-chip ok">0 erros</span>
            </div>

            <div className="mx-tg-note">
              Quebre seu minério, abra a bancada e teste as receitas. Quando fechar o jogo, volte aqui pra continuar forjando.
            </div>
            <button className="mx-save mx-tg-done" onClick={onClose}>Voltar à oficina</button>
          </>
        )}
      </div>
    </div>
  );
}

Object.assign(window, { TestGameOverlay });
