/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Tour de boas-vindas (onboarding guiado)
   Spotlight nos pontos-chave: sidebar → nome → preview → salvar.
   Não bloqueia interação: o usuário pode digitar/editar durante o tour.
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useTourState, useLayoutEffect: useTourLayout, useEffect: useTourEffect } = React;

const TOUR_STEPS = [
  {
    center: true, title: 'Bem-vindo ao Modrix',
    body: 'Vamos criar seu primeiro item juntos — uma espada. Em 4 passos rápidos você vê como o Modrix transforma formulário em mod.',
    cta: 'Começar', sprite: 'sword',
  },
  {
    target: '[data-tour="sidebar-items"]', place: 'right', title: 'Todo conteúdo mora aqui',
    body: 'Itens, blocos, receitas, mobs… organizados por grupo. O botão + cria conteúdo novo de cada tipo.',
  },
  {
    target: '[data-tour="name-field"]', place: 'bottom', title: 'Dê personalidade',
    body: 'Experimente mudar o nome — digite “Espada Flamejante”. O ID de registro se ajusta sozinho.',
  },
  {
    target: '.mx-preview', place: 'left', title: 'Veja como fica no jogo',
    body: 'Cada mudança aparece aqui na hora: no inventário, na mão e na tooltip. Sem compilar nada.',
  },
  {
    target: '.mx-save', place: 'top', title: 'Salve quando gostar',
    body: 'O Modrix regenera o código Java por você. Depois é só clicar em “Testar no jogo”.',
  },
  {
    center: true, title: 'Pronto pra forjar!',
    body: 'Isso é tudo que você precisa pra começar. Explore o Mapa do mod no topo pra ver como os conteúdos se conectam.',
    cta: 'Concluir', sprite: 'sword',
  },
];

function OnboardingTour({ step, onNext, onSkip }) {
  const cfg = TOUR_STEPS[step];
  const [rect, setRect] = useTourState(null);

  // mede o alvo (e re-mede em resize)
  useTourLayout(() => {
    function measure() {
      if (!cfg || cfg.center || !cfg.target) { setRect(null); return; }
      const el = document.querySelector(cfg.target);
      setRect(el ? el.getBoundingClientRect() : null);
    }
    measure();
    window.addEventListener('resize', measure);
    const t = setInterval(measure, 600); // acompanha mudanças de layout
    return () => { window.removeEventListener('resize', measure); clearInterval(t); };
  }, [step]);

  if (!cfg) return null;
  const isLast = step === TOUR_STEPS.length - 1;
  const centered = cfg.center || !rect;

  // posição do card
  let cardStyle;
  if (centered) {
    cardStyle = { left: '50%', top: '50%', transform: 'translate(-50%, -50%)', width: 380 };
  } else {
    const W = 300, pad = 16;
    const cx = Math.max(pad, Math.min(window.innerWidth - W - pad, rect.left));
    if (cfg.place === 'right') cardStyle = { left: rect.right + 16, top: rect.top + rect.height / 2, transform: 'translateY(-50%)', width: W };
    else if (cfg.place === 'left') cardStyle = { left: rect.left - W - 16, top: rect.top + rect.height / 2, transform: 'translateY(-50%)', width: W };
    else if (cfg.place === 'top') cardStyle = { left: cx, top: rect.top - 16, transform: 'translateY(-100%)', width: W };
    else cardStyle = { left: cx, top: rect.bottom + 16, width: W };
  }

  const dots = TOUR_STEPS.length;

  return (
    <div className="mx-tour">
      {/* escurecimento com recorte no alvo */}
      {!centered ? (
        <div className="mx-tour-hole" style={{
          left: rect.left - 6, top: rect.top - 6,
          width: rect.width + 12, height: rect.height + 12,
        }} />
      ) : (
        <div className="mx-tour-veil" />
      )}

      <div className="mx-tour-card" style={cardStyle}>
        {cfg.sprite && (
          <div style={{ display: 'flex', justifyContent: 'center', marginBottom: 10 }}>
            <window.PixelSprite sprite={cfg.sprite} size={52} glint={true} />
          </div>
        )}
        <div className="mx-tour-title">{cfg.title}</div>
        <div className="mx-tour-body">{cfg.body}</div>
        <div className="mx-tour-foot">
          <div className="mx-tour-dots">
            {Array.from({ length: dots }, (_, i) => (
              <span key={i} className={i === step ? 'on' : ''} />
            ))}
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            {!isLast && <button className="mx-tour-skip" onClick={onSkip}>Pular</button>}
            <button className="mx-tour-next" onClick={onNext}>
              {cfg.cta || (isLast ? 'Concluir' : 'Próximo')}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { OnboardingTour, TOUR_STEPS });
