"""Generate a target's startup and catalog glue from its ordered machine prefixes."""
from pathlib import Path
import re

from .definitions import ROOT
from .formats import require
from .stock_tables import EFFECT_COUNT, ENGINE_COUNT, ENGINE_SENTINEL

TEMPLATE = 'sdk/platform/opxy-1.2.1/startup.cpp.in'


def _declare(name, signature='()'):
    return f'int {name}{signature} __asm__("{name}");'


# The companion's calls into the step-lock handles: result, name, parameters, arguments
# and the body without them.
LOCK_CALLS = (('u32', 'size', ('void*', 'u32'), 'return 0;'),
              ('void', 'write', ('void*',), ''),
              ('void', 'begin', (), ''),
              ('void', 'byte', ('u32',), ''),
              ('void', 'loaded', ('void*', 'u32'), ''))


def render(prefixes, effects=(), stock_page=(), sample_screens=(), effect_screens=(), player_pages=(),
           control=False, aux=False, locks=False, drumline=False):
    """stock_page: engines that show a stock page (e.g. sample engines on the
    sampler page); they register on the GUI role without an own-page guard.
    sample_screens: those of them that also draw their own screen over it.
    effect_screens: effects that draw their own page (SDK 1.7), whose screen
    registers on the GUI role.
    control: the build links the control service. Without it, the companion's
    calls into the service do nothing.
    aux: the build links the aux service (aux tracks), which starts after it.
    locks: the build links the step-lock handles (lock_handles.cpp, the drum_locks
    service). Without them, the companion's open-xy.locks calls do nothing.
    drumline: the build links the drumline service (the build option drumlineRow2):
    the shared hooks reach it through sdk_drumline, defined here at link time, so
    no ordering at startup decides whether a hook sees it."""
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
    kept = 'u32 sdk_project_kept(void*) { return 0; }\nvoid sdk_project_loaded(const char*) {}'
    if control:
        declarations += ['u32 sdk_control_kept_write(void*) __asm__("sdk_control_kept_write");',
                         'void sdk_control_kept_load(const char*) __asm__("sdk_control_kept_load");']
        kept = ('u32 sdk_project_kept(void* file) { return sdk_control_kept_write(file); }\n'
                'void sdk_project_loaded(const char* path) { sdk_control_kept_load(path); }')
    for result, name, types, empty in LOCK_CALLS:
        arguments = ', '.join(f'a{i}' for i in range(len(types)))
        if locks:
            declarations.append(f'{result} sdk_step_locks_{name}({", ".join(types)}) __asm__("sdk_step_locks_{name}");')
            parameters = ', '.join(f'{t} a{i}' for i, t in enumerate(types))
            body = ' ' + ('return ' if result != 'void' else '') + f'sdk_step_locks_{name}({arguments}); '
        else:
            parameters, body = ', '.join(types), f' {empty} ' if empty else ''
        kept += f'\n{result} sdk_project_locks_{name}({parameters}) {{{body}}}'
    if aux:
        require(control, 'The aux service needs the control service')
        declarations.append('void sdk_aux_startup() __asm__("sdk_aux_startup");')
        screens.append('    sdk_aux_startup();')
    declarations.append('extern const void* const sdk_drumline __asm__("sdk_drumline");')
    if drumline:
        declarations.append('extern const char sdk_drumline_service[] __asm__("sdk_drumline_service");')
        declarations.append('const void* const sdk_drumline = sdk_drumline_service;')
    else:
        declarations.append('const void* const sdk_drumline = nullptr;')
    owned += [f'    if (page == value({prefix}_fx_page_id)) return {prefix}_ready() == 1;' for prefix in effect_screens]
    sample = [f'    if (engine == value({prefix}_engine_id)) return {prefix}_ready() == 1 && {prefix}_screen_ready() == 1 ? value({prefix}_screen_page_id) : 0;'
              for prefix in sample_screens]
    values = dict(DECLARATIONS='\n'.join(declarations), OWNED_SCREENS='\n'.join(owned), SAMPLE_SCREENS='\n'.join(sample),
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


def generate(directory, prefixes, effects=(), stock_page=(), sample_screens=(), effect_screens=(), player_pages=(),
             control=False, aux=False, locks=False, drumline=False):
    destination = Path(directory) / 'startup.cpp'
    destination.write_text(render(prefixes, effects, stock_page, sample_screens, effect_screens, player_pages,
                                  control, aux, locks, drumline), encoding='utf-8', newline='\n')
    return destination
