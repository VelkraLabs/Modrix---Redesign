/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Editores (Item e Bloco). Controlados; sobem mudanças via onChange.
   ───────────────────────────────────────────────────────────────────────── */

function toId(s) {
  return (s || '').toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '')
    .replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
}

function TexturePicker({ sprite, onCycle, onEdit }) {
  const order = ['sword', 'apple', 'ore', 'rubi', 'vara', 'tabua', 'lingote', 'molde', 'picareta'];
  return (
    <div className="mx-tex-pick filled" onClick={() => {
      const i = order.indexOf(sprite); onCycle(order[(i + 1) % order.length]);
    }} title="Clique pra trocar a textura (demo)">
      <window.PixelSprite sprite={sprite} size={60} />
      <span className="mx-tex-cycle">trocar</span>
      {onEdit && (
        <button type="button" className="mx-tex-edit" title="Pintar textura no pixel editor"
          onClick={e => { e.stopPropagation(); onEdit(sprite); }}>
          <window.Icon name="pen" style="line" size={13} />
        </button>
      )}
    </div>
  );
}

function FieldBlock({ children }) {
  return <div style={{ marginBottom: 18 }}>{children}</div>;
}
function SectionLabel({ children }) {
  return <div style={{ fontSize: 12, fontWeight: 800, letterSpacing: '.8px', textTransform: 'uppercase',
    color: 'var(--text-dim)', margin: '24px 0 12px', paddingBottom: 8, borderBottom: '1px solid var(--border-sub)' }}>{children}</div>;
}

// ── Editor de Item ─────────────────────────────────────────────────────────
function ItemEditor({ rec, modId, set, onEditTexture }) {
  const idTaken = false;
  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Textura</label>
          <TexturePicker sprite={rec.sprite} onCycle={v => set({ sprite: v })} onEdit={onEditTexture} />
        </div>
        <div style={{ flex: 1 }}>
          <FieldBlock>
            <div data-tour="name-field">
              <window.TextField label="Nome de exibição" value={rec.name}
                onChange={v => set({ name: v, id: rec._idEdited ? rec.id : toId(v) })}
                placeholder="Espada de Rubi" hint="Como aparece no jogo." />
            </div>
          </FieldBlock>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: toId(v), _idEdited: true })}
            hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <SectionLabel>Comportamento</SectionLabel>
      <div className="mx-row">
        <window.NumberField label="Tamanho da pilha" value={rec.stackSize} min={1} max={64}
          onChange={v => set({ stackSize: v })} hint="1 = não empilha (armas/ferramentas)." />
        <window.SelectField label="Raridade" value={rec.rarity} options={window.MX_RARITIES}
          onChange={v => set({ rarity: v })} />
      </div>

      <FieldBlock>
        <div style={{ marginTop: 16 }} />
        <label className="mx-label">Tipo de item</label>
        <window.Segmented value={rec.consumable}
          options={[{ id: 'none', label: 'Equipável / arma' }, { id: 'food', label: 'Consumível / comida' }]}
          onChange={v => set({ consumable: v })} />
      </FieldBlock>

      {rec.consumable === 'food' ? (
        <div className="mx-row">
          <window.NumberField label="Nutrição" value={rec.nutrition} min={0} max={20} onChange={v => set({ nutrition: v })} hint="Pontos de fome restaurados." />
          <window.NumberField label="Saturação" value={rec.saturation} step={0.1} min={0} onChange={v => set({ saturation: v })} />
        </div>
      ) : (
        <div className="mx-row">
          <window.NumberField label="Durabilidade" value={rec.durability} min={0} onChange={v => set({ durability: v })} hint="0 = indestrutível." />
          <window.NumberField label="Dano de ataque" value={rec.attackDamage} step={0.5} min={0} onChange={v => set({ attackDamage: v })} />
        </div>
      )}

      <SectionLabel>Propriedades</SectionLabel>
      <div className="mx-box" style={{ display: 'flex', flexDirection: 'column', gap: 13 }}>
        <window.CheckField label="Resistente ao fogo" hint="Não queima na lava." checked={rec.fireResistant} onChange={v => set({ fireResistant: v })} />
        <window.CheckField label="Encantável" hint="Pode receber encantamentos." checked={rec.enchantable} onChange={v => set({ enchantable: v })} />
      </div>
    </div>
  );
}

// ── Editor de Bloco ────────────────────────────────────────────────────────
function BlockEditor({ rec, modId, set, onEditTexture }) {
  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Textura</label>
          <TexturePicker sprite={rec.sprite} onCycle={v => set({ sprite: v })} onEdit={onEditTexture} />
        </div>
        <div style={{ flex: 1 }}>
          <FieldBlock>
            <window.TextField label="Nome de exibição" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : toId(v) })} placeholder="Minério de Rubi" />
          </FieldBlock>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <SectionLabel>Resistência</SectionLabel>
      <div className="mx-row">
        <window.NumberField label="Dureza" value={rec.hardness} step={0.5} min={0} onChange={v => set({ hardness: v })} hint="Tempo pra quebrar." />
        <window.NumberField label="Resistência a explosão" value={rec.resistance} step={0.5} min={0} onChange={v => set({ resistance: v })} />
      </div>

      <SectionLabel>Mineração</SectionLabel>
      <div className="mx-box" style={{ marginBottom: 16 }}>
        <window.CheckField label="Requer ferramenta certa" hint="Senão, não dropa nada." checked={rec.requiresTool} onChange={v => set({ requiresTool: v })} />
      </div>
      {rec.requiresTool && (
        <div className="mx-row">
          <window.SelectField label="Ferramenta" value={rec.tool}
            options={[{ id: 'pickaxe', label: 'Picareta' }, { id: 'axe', label: 'Machado' }, { id: 'shovel', label: 'Pá' }]}
            onChange={v => set({ tool: v })} />
          <window.SelectField label="Nível mínimo" value={rec.toolTier}
            options={[{ id: 'wood', label: 'Madeira' }, { id: 'stone', label: 'Pedra' }, { id: 'iron', label: 'Ferro' }, { id: 'diamond', label: 'Diamante' }]}
            onChange={v => set({ toolTier: v })} />
        </div>
      )}

      <SectionLabel>Drops & Luz</SectionLabel>
      <div className="mx-row">
        <window.TextField label="Item dropado" value={rec.drops} mono onChange={v => set({ drops: v })} />
        <window.NumberField label="Nível de luz" value={rec.lightLevel} min={0} max={15} onChange={v => set({ lightLevel: v })} hint="0–15. >0 ilumina." />
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="XP mínimo" value={rec.xpMin} min={0} onChange={v => set({ xpMin: v })} />
        <window.NumberField label="XP máximo" value={rec.xpMax} min={0} onChange={v => set({ xpMax: v })} />
      </div>
    </div>
  );
}

// ── Editor de Ferramenta ───────────────────────────
function ToolEditor({ rec, modId, set, onEditTexture }) {
  return (
    <div>
      <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start' }}>
        <div>
          <label className="mx-label">Textura</label>
          <TexturePicker sprite={rec.sprite} onCycle={v => set({ sprite: v })} onEdit={onEditTexture} />
        </div>
        <div style={{ flex: 1 }}>
          <FieldBlock>
            <window.TextField label="Nome de exibição" value={rec.name}
              onChange={v => set({ name: v, id: rec._idEdited ? rec.id : toId(v) })} placeholder="Picareta de Rubi" />
          </FieldBlock>
          <window.TextField label="ID de registro" value={rec.id} mono
            onChange={v => set({ id: toId(v), _idEdited: true })} hint={`${modId}:${rec.id || '...'}`} />
        </div>
      </div>

      <SectionLabel>Ferramenta</SectionLabel>
      <FieldBlock>
        <label className="mx-label">Tipo</label>
        <window.Segmented value={rec.toolType}
          options={[{ id: 'pickaxe', label: 'Picareta' }, { id: 'axe', label: 'Machado' }, { id: 'shovel', label: 'Pá' }, { id: 'hoe', label: 'Enxada' }]}
          onChange={v => set({ toolType: v })} />
      </FieldBlock>
      <div className="mx-row">
        <window.SelectField label="Liga / nível" value={rec.tier}
          options={[
            { id: 'wood', label: 'Madeira' }, { id: 'stone', label: 'Pedra' },
            { id: 'iron', label: 'Ferro' }, { id: 'diamond', label: 'Diamante' },
            { id: 'mod', label: `Liga do mod (${modId})` },
          ]}
          onChange={v => set({ tier: v })} />
        <window.SelectField label="Raridade" value={rec.rarity} options={window.MX_RARITIES}
          onChange={v => set({ rarity: v })} />
      </div>

      <SectionLabel>Atributos</SectionLabel>
      <div className="mx-row">
        <window.NumberField label="Durabilidade" value={rec.durability} min={1}
          onChange={v => set({ durability: v })} hint="Ferro = 250 · Diamante = 1561." />
        <window.NumberField label="Velocidade de mineração" value={rec.speed} step={0.5} min={1}
          onChange={v => set({ speed: v })} hint="Ferro = 6 · Diamante = 8." />
      </div>
      <div className="mx-row" style={{ marginTop: 16 }}>
        <window.NumberField label="Dano de ataque" value={rec.attackDamage} step={0.5} min={0}
          onChange={v => set({ attackDamage: v })} hint="Somado ao dano da liga." />
        <window.NumberField label="Velocidade de ataque" value={rec.attackSpeed} step={0.1} max={0}
          onChange={v => set({ attackSpeed: v })} hint="Negativa · picareta = -2.8." />
      </div>

      <SectionLabel>Propriedades</SectionLabel>
      <div className="mx-box" style={{ display: 'flex', flexDirection: 'column', gap: 13 }}>
        <window.CheckField label="Encantável" hint="Eficiência, Fortuna, Inquebrável…" checked={rec.enchantable} onChange={v => set({ enchantable: v })} />
        <window.CheckField label="Resistente ao fogo" hint="Não queima na lava." checked={rec.fireResistant} onChange={v => set({ fireResistant: v })} />
      </div>
    </div>
  );
}

Object.assign(window, { ItemEditor, BlockEditor, ToolEditor, toId });
