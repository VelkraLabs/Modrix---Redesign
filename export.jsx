/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Publicar mod (.jar). Build Gradle simulado com etapas + console,
   terminando num cartão de artefato pronto pra "baixar".
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useExState, useEffect: useExEffect, useRef: useExRef } = React;

const EX_STEPS = [
  { id: 'validate', label: 'Validando conteúdo' },
  { id: 'java',     label: 'Gerando código Java' },
  { id: 'gradle',   label: 'Compilando com Gradle' },
  { id: 'jar',      label: 'Empacotando .jar' },
];

function exLogLines(mod) {
  const n = k => (mod[k] || []).length;
  return [
    [`> modrix validate — ${n('items')} itens · ${n('blocks')} blocos · ${n('recipes')} receitas`,
     `  ✓ ids únicos · texturas ok · receitas válidas`],
    [`> gerando registries: ModItems.java, ModBlocks.java`,
     `  ✓ ${n('items') + n('blocks')} registros · datagen de receitas (${n('recipes')})`],
    [`> gradle build  (NeoForge ${mod.neoforge} · Java 21)`,
     `  ✓ compileJava · processResources · BUILD SUCCESSFUL`],
    [`> empacotando ${mod.modId}-${mod.modVersion}.jar`,
     `  ✓ assinado · pronto para distribuir`],
  ];
}

function ExportModal({ mod, onClose, onPublished }) {
  const [step, setStep] = useExState(0);        // índice da etapa em execução
  const [log, setLog] = useExState([]);
  const [finished, setFinished] = useExState(false);
  const fired = useExRef(false);
  const logRef = useExRef(null);

  useExEffect(() => {
    const lines = exLogLines(mod);
    let cancelled = false;
    let i = 0;
    function next() {
      if (cancelled) return;
      if (i >= EX_STEPS.length) {
        setFinished(true);
        if (!fired.current) { fired.current = true; setTimeout(() => onPublished(), 600); }
        return;
      }
      const idx = i;
      setStep(idx);
      setLog(l => [...l, lines[idx][0]]);
      setTimeout(() => { if (!cancelled) setLog(l => [...l, lines[idx][1]]); }, 420);
      i += 1;
      setTimeout(next, 880);
    }
    const t = setTimeout(next, 350);
    return () => { cancelled = true; clearTimeout(t); };
  }, []);

  useExEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [log]);

  const n = k => (mod[k] || []).length;
  const jarName = `${mod.modId}-${mod.modVersion}.jar`;
  const fakeSize = (180 + n('items') * 14 + n('blocks') * 22 + n('recipes') * 6) + ' KB';

  return (
    <div className="mx-veil" onClick={e => { if (e.target === e.currentTarget && finished) onClose(); }}>
      <div className="mx-export" data-screen-label="Modal Publicar mod">
        <button className="mx-exp-close" onClick={onClose} title="Fechar">✕</button>
        <h2>{finished ? 'Mod publicado!' : `Publicando ${mod.modName}`}</h2>
        <div className="mx-exp-sub">NeoForge {mod.neoforge} · Java 21 · v{mod.modVersion}</div>

        <div className="mx-exp-steps">
          {EX_STEPS.map((s, i) => {
            const state = finished || i < step ? 'done' : i === step ? 'run' : 'wait';
            return (
              <div key={s.id} className={`mx-exp-step ${state}`}>
                <span className="mx-exp-ico">
                  {state === 'done' ? '✓' : state === 'run' ? <span className="mx-exp-spin" /> : '·'}
                </span>
                {s.label}
              </div>
            );
          })}
        </div>

        <pre className="mx-exp-log" ref={logRef}>{log.join('\n')}</pre>

        {finished && (
          <div className="mx-jar">
            <div className="mx-jar-ico"><window.PixelSprite sprite="lingote" size={34} /></div>
            <div className="mx-jar-info">
              <b>{jarName}</b>
              <span>{fakeSize} · {n('items')} itens · {n('blocks')} blocos · {n('recipes')} receitas</span>
            </div>
            <div className="mx-jar-actions">
              <button className="mx-save" onClick={onClose}>Concluir</button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

Object.assign(window, { ExportModal });
