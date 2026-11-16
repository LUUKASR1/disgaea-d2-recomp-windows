"""Read-only guide for the keyboard fallback in the pinned Windows runtime."""
import tkinter as tk
from tkinter import font as tkfont


# These are pad equivalents, not hard-coded actions: the game decides what
# each PS3 button does in the current menu or battle state.
KEY_BINDINGS = (
    ('↑  ↓  ←  →', 'Direcional / analógico esquerdo'),
    ('Z', 'Cruz (×) — confirmar'),
    ('X', 'Círculo (○) — voltar / cancelar'),
    ('A', 'Quadrado (□)'),
    ('S', 'Triângulo (△)'),
    ('Q / W', 'L1 / R1'),
    ('1 / 2', 'L2 / R2'),
    ('Enter', 'START'),
    ('Tab', 'SELECT'),
)


def build_controls_guide(parent, translate):
    """Build scrollable-page content; return a language-refresh callback.

    Use installed system fonts, never bundle an extracted game font. Ordinary
    labels are localized by the launcher's existing widget_bindings; Canvas
    title text must be refreshed explicitly.
    """
    bg, panel, ink, muted, gold = '#130e1d', '#2b1c38', '#fff2e6', '#b7a7c5', '#ffb16a'
    families = set(tkfont.families(parent))
    title_family = 'Impact' if 'Impact' in families else 'Segoe UI'
    title_font = tkfont.Font(parent, family=title_family, size=29, weight='bold')
    # Keep the named Font alive for the Canvas, which stores only its Tcl name.
    parent._controls_title_font = title_font
    banner = tk.Canvas(parent, height=62, bg=bg, highlightthickness=0)
    banner.pack(fill='x', pady=(0, 8))
    title_ids = []
    for dx, dy in ((-2, -2), (-2, 2), (2, -2), (2, 2), (0, 3)):
        title_ids.append(banner.create_text(6 + dx, 30 + dy, anchor='w',
                         font=title_font, fill='#07050c'))
    title_ids.append(banner.create_text(6, 30, anchor='w', font=title_font, fill=gold))

    def label(box, text, color=ink, size=11, bold=False):
        result = tk.Label(box, text=text, fg=color, bg=box['bg'], anchor='w',
                          justify='left', font=('Segoe UI', size, 'bold' if bold else 'normal'))
        result.pack(anchor='w', fill='x', pady=(0, 8))
        # Adapt text wrapping to the actual window width, including English.
        result.bind('<Configure>', lambda e: result.configure(wraplength=max(120, e.width - 4)))
        return result

    label(parent, 'COMANDOS DO JOGADOR', gold, 11, True)
    label(parent, 'Use o teclado com a janela do jogo em foco e sem controle conectado. '
                  'Um controle conectado tem prioridade sobre o teclado.', muted)
    table = tk.Frame(parent, bg=panel, padx=18, pady=14)
    table.pack(fill='x', pady=(4, 14))
    table.grid_columnconfigure(1, weight=1)
    for row, (key, action) in enumerate((('TECLA', 'BOTÃO / FUNÇÃO'),) + KEY_BINDINGS):
        tk.Label(table, text=key, bg=panel, fg=gold,
                 font=('Segoe UI', 11, 'bold'), anchor='w', padx=6, pady=7).grid(row=row, column=0, sticky='nw')
        value = tk.Label(table, text=action, bg=panel, fg=gold if row == 0 else ink,
                         font=('Segoe UI', 11, 'bold' if row == 0 else 'normal'),
                         anchor='w', justify='left', padx=14, pady=7)
        value.grid(row=row, column=1, sticky='ew')
        value.bind('<Configure>', lambda e, w=value: w.configure(wraplength=max(120, e.width - 28)))
    label(parent, 'As funções dos botões podem variar conforme o menu e a situação da batalha. '
                  'Este guia mostra o mapeamento atual; não altera os comandos.', muted, 10)
    mouse = tk.Frame(parent, bg=panel, padx=18, pady=14)
    mouse.pack(fill='x', pady=(4, 14))
    label(mouse, 'MOUSE', gold, 14, True)
    label(mouse, 'No launcher: clique nos botões e nas opções normalmente.')
    label(mouse, 'Dentro do jogo: seleção direta de menus, personagens e casas pelo ponteiro '
                 'ainda não está disponível. Use o teclado ou um controle.', muted)
    label(parent, 'DICA DO PRINNY', gold, 12, True)
    label(parent, 'O teclado não respondeu? Desconecte o controle e clique na janela do jogo '
                  'para devolver o foco a ela. Ao trocar de janela, o jogo deixa de receber as teclas.', muted)

    def refresh():
        for item in title_ids:
            banner.itemconfigure(item, text=translate('Guia de comandos'))
    refresh()
    return refresh
