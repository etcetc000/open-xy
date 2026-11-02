"""Generate a target's startup and catalog glue from its ordered machine prefixes."""
from pathlib import Path
import re

from .definitions import ROOT
from .formats import require
from .stock_tables import EFFECT_COUNT, ENGINE_COUNT, ENGINE_SENTINEL

TEMPLATE = 'sdk/platform/opxy-1.2.1/startup.cpp.in'


def _declare(name, signature='()'):
    return f'int {name}{signature} __asm__("{name}");'


def render(prefixes, effects=(), stock_page=(), sample_screens=(), effect_screens=(), player_pages=()):
    """stock_page: engines that show a stock page (e.g. sample engines on the
    sampler page); they register on the GUI role without an own-page guard.
    sample_screens: those of them that also draw their own screen over it.
    effect_screens: effects that draw their own page (SDK 1.7), whose screen
    registers on the GUI role."""
    require(prefixes or effects or player_pages, 'Startup needs at least one machine')
    require(0 <= len(prefixes) <= ENGINE_COUNT and len(set(prefixes)) == len(prefixes),
            'Startup needs distinct engine prefixes')
    require(len(set(effects)) == len(effects) <= EFFECT_COUNT and not set(effects) & set(prefixes),
            'Startup needs distinct effect prefixes')
    require(set(stock_page) <= set(prefixes), 'Stock-page engines must be listed engines')
    require(set(sample_screens) <= set(stock_page), 'Sample screens must be stock-page engines')
    require(len(set(player_pages)) == len(player_pages) and not set(player_pages) & (set(prefixes) | set(effects)),
            'Startup needs distinct player prefixes')
    for prefix in (*prefixes, *effects, *player_pages):
        require(re.fullmatch(r'[a-z][a-z0-9_]{0,31}', prefix) is not None, 'Invalid engine prefix')
    declarations = [_declare(prefix + '_fx_register') for prefix in effects]
    declarations += [_declare(prefix + '_screen_register') for prefix in player_pages]
    require(set(effect_screens) <= set(effects), 'Effects with their own page must be listed effects')
    for prefix in effect_screens:
        declarations += [_declare(prefix + '_screen_register'), _declare(prefix + '_ready'),
                         f'extern const char {prefix}_fx_page_id[] __asm__("{prefix}_fx_page_id");']
    for prefix in prefixes:
        declarations += [_declare(prefix + '_register'), _declare(prefix + '_name_matches', '(const void*)'),
                         _declare(prefix + '_ready'), f'extern const char {prefix}_engine_id[] __asm__("{prefix}_engine_id");']
        if prefix not in stock_page:
            declarations += [_declare(prefix + '_screen_register'),
                             f'extern const char {prefix}_page_id[] __asm__("{prefix}_page_id");']
        if prefix in sample_screens:
            declarations += [_declare(prefix + '_screen_register'), _declare(prefix + '_screen_ready'),
                             f'extern const char {prefix}_screen_page_id[] __asm__("{prefix}_screen_page_id");']
    # An effect is registered on both roles already; its page, which its record
    # names, is ready once its screen is in place.
    screens = [f'    {prefix}_screen_register();' for prefix in effect_screens]
    for prefix in prefixes:
        guard = '' if prefix in stock_page else f'if ({prefix}_screen_register() == 1) '
        # A sample engine's own screen never holds back the engine: it plays without it.
        if prefix in sample_screens:
            screens.append(f'    {prefix}_screen_register();')
        screens.append(f'    {guard}{prefix}_register();')
    screens += [f'    {prefix}_screen_register();' for prefix in player_pages]
    catalog = [f'    if ({prefix}_name_matches(name))\n'
               f'        return id == value({prefix}_engine_id) && {prefix}_ready() == 1 ? '
               'stock_catalog_insert(catalog, name, id) : 0;' for prefix in prefixes]
    owned = [f'    if (page == value({prefix}_page_id)) return {prefix}_ready() == 1;'
             for prefix in prefixes if prefix not in stock_page]
    owned += [f'    if (page == value({prefix}_fx_page_id)) return {prefix}_ready() == 1;' for prefix in effect_screens]
    sample = [f'    if (engine == value({prefix}_engine_id)) return {prefix}_ready() == 1 && {prefix}_screen_ready() == 1 ? value({prefix}_screen_page_id) : 0;'
              for prefix in sample_screens]
    values = dict(DECLARATIONS='\n'.join(declarations), OWNED_SCREENS='\n'.join(owned), SAMPLE_SCREENS='\n'.join(sample),
                  EFFECTS='\n'.join(f'    {prefix}_fx_register();' for prefix in effects),
                  ENGINES='\n'.join(f'    {prefix}_register();' for prefix in prefixes),
                  SCREENS='\n'.join(screens), CATALOG='\n'.join(catalog), ENGINE_SENTINEL=str(ENGINE_SENTINEL),
                  INSTALLED='\n'.join(f'    if ({prefix}_name_matches(name)) return 1;' for prefix in prefixes))
    source = (ROOT / TEMPLATE).read_text(encoding='utf-8')
    for key, text in values.items():
        require(source.count('@' + key + '@') == 1, 'Startup template placeholder missing')
        source = source.replace('@' + key + '@', text)
    require('@' not in source, 'Unexpanded startup template')
    return source


def generate(directory, prefixes, effects=(), stock_page=(), sample_screens=(), effect_screens=(), player_pages=()):
    destination = Path(directory) / 'startup.cpp'
    destination.write_text(render(prefixes, effects, stock_page, sample_screens, effect_screens, player_pages), encoding='utf-8', newline='\n')
    return destination
