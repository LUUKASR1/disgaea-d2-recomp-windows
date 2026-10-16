"""Presentation-only localization; persisted separately from graphics/game data."""
import json
import os
from pathlib import Path
import re
import sys
import uuid

LANGUAGES = {'pt-BR': 'Português (Brasil)', 'en': 'English'}


def load_language(home):
    try:
        value = json.loads((Path(home) / 'launcher-language.json').read_text(encoding='utf-8'))
        language = value.get('language') if isinstance(value, dict) else None
        return language if language in LANGUAGES else 'pt-BR'
    except (OSError, ValueError, TypeError):
        return 'pt-BR'


def save_language(home, language):
    if not isinstance(language, str) or language not in LANGUAGES:
        raise ValueError('Unsupported launcher language.')
    home = Path(home); home.mkdir(parents=True, exist_ok=True)
    target = home / 'launcher-language.json'
    temporary = home / ('launcher-language-' + uuid.uuid4().hex + '.tmp')
    try:
        temporary.write_text(json.dumps({'schema': 1, 'language': language}) + '\n', encoding='utf-8')
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


class Localizer:
    def __init__(self, language='pt-BR'):
        root = Path(sys._MEIPASS) / 'windows' if getattr(sys, 'frozen', False) else Path(__file__).parent
        self.catalog = json.loads((root / 'locales/en.json').read_text(encoding='utf-8'))
        self.reverse = {value: key for key, value in self.catalog.items() if '{' not in key}
        self.templates = []
        for source, translated in self.catalog.items():
            if '{' not in source:
                continue
            parts = re.split(r'(\{\w+\})', source)
            pattern = ''.join('(?P<' + part[1:-1] + '>.*?)' if part.startswith('{')
                              else re.escape(part) for part in parts)
            self.templates.append((re.compile(pattern, re.DOTALL), translated))
        self.language = language if language in LANGUAGES else 'pt-BR'

    def source(self, value):
        return self.reverse.get(value, value)

    def __call__(self, value, _depth=0):
        if self.language != 'en' or not isinstance(value, str) or _depth > 8:
            return value
        if value in self.catalog:
            return self.catalog[value]
        for pattern, translated in self.templates:
            match = pattern.fullmatch(value)
            if match:
                # Translate known UI enum values; keep filenames/paths verbatim.
                return translated.format_map({k: self(v, _depth + 1) for k, v in match.groupdict().items()})
        if ' • ' in value:
            return ' • '.join(self(part, _depth + 1) for part in value.split(' • '))
        return value  # Preserve original technical errors and unknown messages.


class Dialogs:
    def __init__(self, module, translate):
        self.module, self.translate = module, translate

    def __getattr__(self, name):
        def call(*args, **kwargs):
            args = tuple(self.translate(value) for value in args)
            if 'title' in kwargs:
                kwargs['title'] = self.translate(kwargs['title'])
            if 'filetypes' in kwargs:
                kwargs['filetypes'] = [(self.translate(label), pattern) for label, pattern in kwargs['filetypes']]
            return getattr(self.module, name)(*args, **kwargs)
        return call


def text_variable_class(tk, translate):
    class TextVariable(tk.StringVar):
        def __init__(self, *args, value='', choice=False, **kwargs):
            self.source_value, self.choice = value, choice
            super().__init__(*args, value=translate(value), **kwargs)

        def set(self, value):
            self.source_value = value
            super().set(translate(value))

        def get(self):
            return translate.source(super().get()) if self.choice else self.source_value

        def refresh(self):
            self.set(self.get())
    return TextVariable


def widget_bindings(app, translate, excluded=()):
    """Capture original UI labels once; never change commands, IDs or paths."""
    bindings = []
    def visit(widget):
        if widget not in excluded:
            options = widget.keys()
            if 'text' in options and not ('textvariable' in options and widget.cget('textvariable')) and widget.cget('text'):
                source = widget.cget('text')
                bindings.append(lambda w=widget, s=source: w.configure(text=translate(s)))
            if 'values' in options:
                values = tuple(widget.cget('values'))
                bindings.append(lambda w=widget, v=values: w.configure(values=tuple(translate(s) for s in v)))
        for child in widget.winfo_children():
            visit(child)
    visit(app)
    return bindings
