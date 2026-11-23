"""
Plume — génère les logos des applis Plume (void/icones/plume-*.svg).
  python3 void/icones/generer.py void/icones
"""
import os, sys
OUT = sys.argv[1]
# fond : carré arrondi 112 px dans 128, dégradé propre à l'appli, reflet en haut, liseré clair
def icon(name, c1, c2, body, angle=(0, 0, 1, 1)):
    x1, y1, x2, y2 = angle
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <!-- Plume Linux — icône « {name} » -->
  <defs>
    <linearGradient id="f" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">
      <stop offset="0" stop-color="{c1}"/><stop offset="1" stop-color="{c2}"/>
    </linearGradient>
    <linearGradient id="r" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#fff" stop-opacity=".28"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/>
    </linearGradient>
    <filter id="o" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="2.2"/><feOffset dy="2.5" result="b"/>
      <feComponentTransfer><feFuncA type="linear" slope=".35"/></feComponentTransfer>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
  </defs>
  <rect x="8" y="8" width="112" height="112" rx="27" fill="url(#f)"/>
  <rect x="8" y="8" width="112" height="112" rx="27" fill="url(#r)"/>
  <rect x="8.75" y="8.75" width="110.5" height="110.5" rx="26.3" fill="none" stroke="#fff" stroke-opacity=".22" stroke-width="1.5"/>
  <g filter="url(#o)" fill="none" stroke="#fff" stroke-width="7" stroke-linecap="round" stroke-linejoin="round">
{body}
  </g>
</svg>
'''
    open(os.path.join(OUT, name + '.svg'), 'w').write(svg)

W = '#fff'
icon('plume-wifi', '#1e7bff', '#00d2ff', '''    <path d="M30 56a48 48 0 0 1 68 0"/>
    <path d="M40 67a34 34 0 0 1 48 0"/>
    <path d="M50 78a20 20 0 0 1 28 0"/>
    <circle cx="64" cy="90" r="5.5" fill="#fff" stroke="none"/>''')
icon('plume-son', '#ff3d71', '#ffa040', '''    <path d="M34 54h12l16-14v48L46 74H34z" fill="#fff"/>
    <path d="M74 52a17 17 0 0 1 0 24"/>
    <path d="M83 43a30 30 0 0 1 0 42"/>''')
icon('plume-bluetooth', '#3d4dff', '#00aaff', '''    <path d="M46 46l36 34-18 16V32l18 16-36 34"/>''')
icon('plume-fonds', '#8e2de2', '#ff6ec4', '''    <path d="M64 30c-19 0-34 14-34 32s14 34 32 34c6 0 8-4 6-8s0-8 5-8h9c9 0 16-7 16-16 0-19-15-34-34-34z" fill="#fff" fill-opacity=".18"/>
    <circle cx="48" cy="54" r="5.5" fill="#ffd34e" stroke="none"/>
    <circle cx="64" cy="44" r="5.5" fill="#4fe0a4" stroke="none"/>
    <circle cx="80" cy="54" r="5.5" fill="#4fb8e0" stroke="none"/>
    <circle cx="46" cy="72" r="5.5" fill="#ff5470" stroke="none"/>''')
icon('plume-logitheque', '#00c9a7', '#0077ff', '''    <path d="M38 50h52l-4 42H42z" fill="#fff" fill-opacity=".18"/>
    <path d="M50 50v-6a14 14 0 0 1 28 0v6"/>
    <path d="M64 60v20M56 72l8 8 8-8"/>''')
icon('plume-term', '#2b3442', '#0b1220', '''    <path d="M38 50l14 13-14 13" stroke="#4fe0a4"/>
    <path d="M60 80h28" stroke="#4fe0a4"/>''')
icon('plume-pilotage', '#0f2a4a', '#0a5c8a', '''    <path d="M34 82a34 34 0 1 1 60 0" stroke="#7fe3ff"/>
    <path d="M64 74l16-20" stroke="#fff"/>
    <circle cx="64" cy="74" r="6" fill="#fff" stroke="none"/>
    <path d="M44 62h0M54 50h0M74 50h0M84 62h0" stroke="#7fe3ff" stroke-width="5"/>''')
icon('plume-lanceur', '#3a4352', '#121722', '''    <circle cx="42" cy="42" r="7" fill="#ff5f57" stroke="none"/>
    <circle cx="64" cy="42" r="7" fill="#febc2e" stroke="none"/>
    <circle cx="86" cy="42" r="7" fill="#28c840" stroke="none"/>
    <circle cx="42" cy="64" r="7" fill="#4fb8e0" stroke="none"/>
    <circle cx="64" cy="64" r="7" fill="#b07cff" stroke="none"/>
    <circle cx="86" cy="64" r="7" fill="#ff6ec4" stroke="none"/>
    <circle cx="42" cy="86" r="7" fill="#4fe0a4" stroke="none"/>
    <circle cx="64" cy="86" r="7" fill="#ffa040" stroke="none"/>
    <circle cx="86" cy="86" r="7" fill="#fff" stroke="none"/>''')
icon('plume-ecrans', '#2563eb', '#7c3aed', '''    <rect x="30" y="36" width="68" height="44" rx="6" fill="#fff" fill-opacity=".18"/>
    <path d="M64 80v12M48 94h32"/>''')
icon('plume-vpn', '#047857', '#22c55e', '''    <path d="M64 30l28 10v20c0 18-12 31-28 38-16-7-28-20-28-38V40z" fill="#fff" fill-opacity=".18"/>
    <rect x="52" y="60" width="24" height="18" rx="3" fill="#fff" stroke="none"/>
    <path d="M57 60v-5a7 7 0 0 1 14 0v5" stroke-width="5"/>''')
icon('plume-batterie', '#65c80d', '#0f9d58', '''    <rect x="28" y="46" width="64" height="36" rx="8"/>
    <path d="M98 58v12" stroke-width="8"/>
    <path d="M64 50l-10 15h12l-8 13" stroke-width="6"/>''')
icon('plume-nuit', '#1e1b4b', '#6d28d9', '''    <path d="M76 34a30 30 0 1 0 18 46 24 24 0 0 1-18-46z" fill="#ffd772" stroke="#ffd772" stroke-width="3"/>
    <path d="M86 38l2 5 5 2-5 2-2 5-2-5-5-2 5-2z" fill="#fff" stroke="none"/>
    <circle cx="96" cy="58" r="2.5" fill="#fff" stroke="none"/>''', (0, 1, 1, 0))
icon('plume-sauvegarde', '#f59e0b', '#ef4444', '''    <path d="M44 80a16 16 0 0 1-2-32 22 22 0 0 1 42-4 18 18 0 0 1 4 36z" fill="#fff" fill-opacity=".18"/>
    <path d="M64 88V60M54 70l10-10 10 10"/>''')
icon('plume-accessibilite', '#0ea5e9', '#2563eb', '''    <circle cx="64" cy="64" r="34" stroke-width="5"/>
    <circle cx="64" cy="44" r="6" fill="#fff" stroke="none"/>
    <path d="M44 56l20 4 20-4M64 60v14M64 74l-10 16M64 74l10 16" stroke-width="6"/>''')
icon('plume-retour', '#14b8a6', '#0e5c8a', '''    <path d="M38 64a26 26 0 1 0 8-19"/>
    <path d="M44 32v14h14"/>
    <path d="M64 50v15l10 7" stroke-width="6"/>''')
icon('plume-raccourcis', '#64748b', '#273244', '''    <rect x="28" y="42" width="72" height="46" rx="8" fill="#fff" fill-opacity=".14"/>
    <path d="M40 56h0M52 56h0M64 56h0M76 56h0M88 56h0M46 68h0M58 68h0M70 68h0M82 68h0" stroke-width="7"/>
    <path d="M48 78h32" stroke-width="6"/>''')
icon('plume-notifications', '#f43f5e', '#f97316', '''    <path d="M64 32c-13 0-22 10-22 23v14l-7 11h58l-7-11V55c0-13-9-23-22-23z" fill="#fff" fill-opacity=".18"/>
    <path d="M56 88a8 8 0 0 0 16 0"/>
    <circle cx="86" cy="40" r="8" fill="#fff" stroke="none"/>''')
print(len(os.listdir(OUT)), 'icônes')
