"""Generate a target's startup and catalog glue from its ordered machine prefixes."""
from pathlib import Path
import re

from .definitions import ROOT
from .formats import require
from .stock_tables import ENGINE_SENTINEL

TEMPLATE = 'sdk/platform/opxy-1.2.1/startup.cpp.in'
MAX_ENGINES = 32


def _declare(name, signature='()'):
    return f'int {name}{signature} __asm__("{name}");'


def render(prefixes, effects=(), stock_page=()):
    """stock_page: engines that show a stock page (e.g. sample engines on the
    sampler page); they register on the GUI role without an own-page guard."""
    # A collection of step components alone registers no engines.
    require(len(prefixes) <= MAX_ENGINES and len(set(prefixes)) == len(prefixes),
            'Startup needs distinct engine prefixes')
    require(len(set(effects)) == len(effects) <= MAX_ENGINES and not set(effects) & set(prefixes),
            'Startup needs distinct effect prefixes')
    require(set(stock_page) <= set(prefixes), 'Stock-page engines must be listed engines')
    for prefix in (*prefixes, *effects):
        require(re.fullmatch(r'[a-z][a-z0-9_]{0,31}', prefix) is not None, 'Invalid engine prefix')
    declarations = [_declare(prefix + '_fx_register') for prefix in effects]
    for prefix in prefixes:
        declarations += [_declare(prefix + '_register'), _declare(prefix + '_name_matches', '(const void*)'),
                         _declare(prefix + '_ready'), f'extern const char {prefix}_engine_id[] __asm__("{prefix}_engine_id");']
        if prefix not in stock_page:
            declarations += [_declare(prefix + '_screen_register'),
                             f'extern const char {prefix}_page_id[] __asm__("{prefix}_page_id");']
    screens = []
    for prefix in prefixes:
        guard = '' if prefix in stock_page else f'if ({prefix}_screen_register() == 1) '
        screens.append(f'    {guard}{prefix}_register();')
    catalog = [f'    if ({prefix}_name_matches(name))\n'
               f'        return id == value({prefix}_engine_id) && {prefix}_ready() == 1 ? '
               'stock_catalog_insert(catalog, name, id) : 0;' for prefix in prefixes]
    owned = [f'    if (page == value({prefix}_page_id)) return {prefix}_ready() == 1;'
             for prefix in prefixes if prefix not in stock_page]
    values = dict(DECLARATIONS='\n'.join(declarations), OWNED_SCREENS='\n'.join(owned),
                  EFFECTS='\n'.join(f'    {prefix}_fx_register();' for prefix in effects),
                  ENGINES='\n'.join(f'    {prefix}_register();' for prefix in prefixes),
                  SCREENS='\n'.join(screens), CATALOG='\n'.join(catalog), ENGINE_SENTINEL=str(ENGINE_SENTINEL))
    source = (ROOT / TEMPLATE).read_text(encoding='utf-8')
    for key, text in values.items():
        require(source.count('@' + key + '@') == 1, 'Startup template placeholder missing')
        source = source.replace('@' + key + '@', text)
    require('@' not in source, 'Unexpanded startup template')
    return source


def generate(directory, prefixes, effects=(), stock_page=()):
    destination = Path(directory) / 'startup.cpp'
    destination.write_text(render(prefixes, effects, stock_page), encoding='utf-8', newline='\n')
    return destination
