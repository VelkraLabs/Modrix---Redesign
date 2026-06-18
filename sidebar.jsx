/* ─────────────────────────────────────────────────────────────────────────
   Modrix — Sidebar agrupada (navegação de conteúdo do mod)
   ───────────────────────────────────────────────────────────────────────── */
const { useState: useSbState } = React;

function Sidebar({ mod, iconStyle, selectedKey, selectedId, onSelect, onAdd, onPack }) {
  const [q, setQ] = useSbState('');
  const query = q.trim().toLowerCase();

  // resolve entradas de cada seção (dinâmicas vêm do mod; stubs são exemplos)
  function entriesFor(sec) {
    if (sec.dynamic) {
      return (mod[sec.dynamic] || []).map(r => ({ id: r.id, name: r.name, sub: `${mod.modId}:${r.id}`, sprite: r.sprite, real: true, isNew: !!r._new }));
    }
    return (sec.stub || []).map((name, i) => ({ id: `${sec.key}-${i}`, name, sub: '', stub: true }));
  }

  return (
    <nav className="mx-sidebar">
      <div className="mx-side-search">
        <input className="mx-input" placeholder="Buscar conteúdo…" value={q} onChange={e => setQ(e.target.value)} />
      </div>

      {onPack && (
        <button type="button" className="mx-side-pack" onClick={onPack} title="Adicionar vários registros encadeados">
          <window.Icon name="grid" style={iconStyle} size={14} />
          Conjunto pronto
        </button>
      )}

      <button type="button" className={`mx-overview-btn${selectedKey === 'overview' ? ' on' : ''}`}
        onClick={() => onSelect('overview', null)}>
        <window.Icon name="cube" style={iconStyle} size={15} />
        Visão geral
      </button>

      {window.MX_SIDEBAR.map(group => {
        // filtra seções/entradas pela busca
        const secs = group.sections.map(sec => {
          let entries = entriesFor(sec);
          if (query) entries = entries.filter(e => e.name.toLowerCase().includes(query) || sec.name.toLowerCase().includes(query));
          return { sec, entries };
        }).filter(({ sec, entries }) => !query || entries.length || sec.name.toLowerCase().includes(query));

        if (!secs.length) return null;
        return (
          <div className="mx-group" key={group.label}>
            <div className="mx-group-label">{group.label}</div>
            {secs.map(({ sec, entries }) => (
              <div className="mx-sec" key={sec.key} data-tour={sec.key === 'items' ? 'sidebar-items' : null}>
                <div className="mx-sec-head">
                  <span className={`mx-sec-title${selectedKey === sec.dynamic ? ' on' : ''}`}
                    onClick={() => onSelect(sec.dynamic, entries.length ? entries[0].id : null)}
                    title={`Abrir ${sec.name}`}>
                    <span className="mx-sec-ico"><window.Icon name={sec.ico} style={iconStyle} size={17} /></span>
                    <span className="mx-sec-name">{sec.name}</span>
                    <span className="mx-count">{entries.length}</span>
                    {entries.some(e => e.isNew) && <span className="mx-sec-newdot" title="novidades desde a última publicação" />}
                  </span>
                  <button className="mx-add" title={`Novo: ${sec.name}`} onClick={() => onAdd(sec)}>+</button>
                </div>
                <div className="mx-entries">
                  {entries.length === 0 && !query && (
                    <button className="mx-empty-cta" onClick={() => onAdd(sec)}>+ criar o primeiro</button>
                  )}
                  {entries.map(e => {
                    const active = e.real && sec.dynamic === selectedKey && e.id === selectedId;
                    return (
                      <div key={e.id} className={`mx-entry${active ? ' active' : ''}`}
                        onClick={() => e.real ? onSelect(sec.dynamic, e.id) : null}
                        style={!e.real ? { opacity: .62, cursor: 'default' } : null}>
                        <span className="mx-entry-ico">
                          {e.sprite
                            ? <window.PixelSprite sprite={e.sprite} size={20} />
                            : <window.Icon name={sec.ico} style={iconStyle} size={14} />}
                        </span>
                        <span style={{ minWidth: 0 }}>
                          <div className="mx-entry-name">{e.name}{e.isNew && <span className="mx-new-pill">novo</span>}</div>
                          {e.sub && <div className="mx-entry-sub">{e.sub}</div>}
                        </span>
                      </div>
                    );
                  })}
                </div>
              </div>
            ))}
          </div>
        );
      })}
      <div style={{ height: 14 }} />
    </nav>
  );
}

Object.assign(window, { Sidebar });
