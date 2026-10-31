"""Generate a target's startup and catalog glue from its ordered machine prefixes."""
from pathlib import Path
import re

from .definitions import ROOT
from .formats import require
from .stock_tables import EFFECT_COUNT, ENGINE_COUNT, ENGINE_SENTINEL

TEMPLATE = 'sdk/platform/opxy-1.2.1/startup.cpp.in'


def _declare(name, signature='()'):
    return f'int {name}{signature} __asm__("{name}");'


def render(prefixes, effects=(), stock_page=(), effect_screens=(), control=False):
    """stock_page: engines that show a stock page (e.g. sample engines on the
    sampler page); they register on the GUI role without an own-page guard.
    effect_screens: effects that draw their own page (SDK 1.7), whose screen
    registers on the GUI role.
    control: the build links the control service; its marks screen registers
    last. Without it, the companion's calls into the service do nothing."""
    require(1 <= len(prefixes) <= ENGINE_COUNT and len(set(prefixes)) == len(prefixes),
            'Startup needs distinct engine prefixes')
    require(len(set(effects)) == len(effects) <= EFFECT_COUNT and not set(effects) & set(prefixes),
            'Startup needs distinct effect prefixes')
    require(set(stock_page) <= set(prefixes), 'Stock-page engines must be listed engines')
    require(set(effect_screens) <= set(effects), 'Effects with their own page must be listed effects')
    for prefix in (*prefixes, *effects):
        require(re.fullmatch(r'[a-z][a-z0-9_]{0,31}', prefix) is not None, 'Invalid engine prefix')
    declarations = [_declare(prefix + '_fx_register') for prefix in effects]
    for prefix in effect_screens:
        declarations += [_declare(prefix + '_screen_register'), _declare(prefix + '_ready'),
                         f'extern const char {prefix}_fx_page_id[] __asm__("{prefix}_fx_page_id");']
    for prefix in prefixes:
        declarations += [_declare(prefix + '_register'), _declare(prefix + '_name_matches', '(const void*)'),
                         _declare(prefix + '_ready'), f'extern const char {prefix}_engine_id[] __asm__("{prefix}_engine_id");']
        if prefix not in stock_page:
            declarations += [_declare(prefix + '_screen_register'),
                             f'extern const char {prefix}_page_id[] __asm__("{prefix}_page_id");']
    # An effect is registered on both roles already; its page, which its record
    # names, is ready once its screen is in place.
    screens = [f'    {prefix}_screen_register();' for prefix in effect_screens]
    for prefix in prefixes:
        guard = '' if prefix in stock_page else f'if ({prefix}_screen_register() == 1) '
        screens.append(f'    {guard}{prefix}_register();')
    catalog = [f'    if ({prefix}_name_matches(name))\n'
               f'        return id == value({prefix}_engine_id) && {prefix}_ready() == 1 ? '
               'stock_catalog_insert(catalog, name, id) : 0;' for prefix in prefixes]
    owned = [f'    if (page == value({prefix}_page_id)) return {prefix}_ready() == 1;'
             for prefix in prefixes if prefix not in stock_page]
    kept = 'u32 sdk_project_kept(void*) { return 0; }\nvoid sdk_project_loaded(const char*) {}'
    if control:
        declarations += ['void sdk_overlay_startup() __asm__("sdk_overlay_startup");',
                         'u32 sdk_control_kept_write(void*) __asm__("sdk_control_kept_write");',
                         'void sdk_control_kept_load(const char*) __asm__("sdk_control_kept_load");']
        screens.append('    sdk_overlay_startup();')
        kept = ('u32 sdk_project_kept(void* file) { return sdk_control_kept_write(file); }\n'
                'void sdk_project_loaded(const char* path) { sdk_control_kept_load(path); }')
    owned += [f'    if (page == value({prefix}_fx_page_id)) return {prefix}_ready() == 1;' for prefix in effect_screens]
    values = dict(DECLARATIONS='\n'.join(declarations), OWNED_SCREENS='\n'.join(owned),
                  EFFECTS='\n'.join(f'    {prefix}_fx_register();' for prefix in effects),
                  ENGINES='\n'.join(f'    {prefix}_register();' for prefix in prefixes),
                  SCREENS='\n'.join(screens), CATALOG='\n'.join(catalog), ENGINE_SENTINEL=str(ENGINE_SENTINEL),
                  PROJECT_KEPT=kept,
                  INSTALLED='\n'.join(f'    if ({prefix}_name_matches(name)) return 1;' for prefix in prefixes))
    source = (ROOT / TEMPLATE).read_text(encoding='utf-8')
    for key, text in values.items():
        require(source.count('@' + key + '@') == 1, 'Startup template placeholder missing')
        source = source.replace('@' + key + '@', text)
    require('@' not in source, 'Unexpanded startup template')
    return source


def generate(directory, prefixes, effects=(), stock_page=(), effect_screens=(), control=False):
    destination = Path(directory) / 'startup.cpp'
    destination.write_text(render(prefixes, effects, stock_page, effect_screens, control), encoding='utf-8', newline='\n')
    return destination
