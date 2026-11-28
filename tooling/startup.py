"""Generate a target's startup and catalog glue for the services it links."""
from pathlib import Path

from .definitions import ROOT
from .formats import require
from .stock_tables import ENGINE_SENTINEL

TEMPLATE = 'sdk/platform/opxy-1.2.1/startup.cpp.in'


# Every call into the step-lock handles (lock_handles.cpp, the drum_locks service) from
# units every build links: result, name, parameters and the body without the service.
# The companion's open-xy.locks (size, write, begin, byte, loaded); a padMix kit's or a
# Shift-row synth's held step, which its Shift row edits and shows (held_value, held_set); and the GUI frame,
# which expires a step note's handle that no note start took (frame). Without the
# service each answers 0 or does nothing, as the browser builder's fallback kit_absent does.
LOCK_CALLS = (('u32', 'size', ('void*', 'u32'), 'return 0;'),
              ('void', 'write', ('void*',), ''),
              ('void', 'begin', (), ''),
              ('void', 'byte', ('u32',), ''),
              ('void', 'loaded', ('void*', 'u32'), ''),
              ('u32', 'held_value', ('u32', 'u32', 'u32', 'i32*'), 'return 0;'),
              ('u32', 'held_set', ('u32', 'u32', 'u32', 'u32', 'i32'), 'return 0;'),
              ('void', 'frame', (), ''))


def render(control=False, aux=False, locks=False, drumline=False):
    """The startup and catalog glue (startup.cpp.in), which registers the selected machines
    from their records (machine_record.hpp) at run time: only the services a build links
    shape it, never its machines.
    control: the build links the control service. Without it, the companion's
    calls into the service do nothing.
    aux: the build links the aux service (aux tracks), which starts after it.
    locks: the build links the step-lock handles (lock_handles.cpp, the drum_locks
    service). Without them, the companion's open-xy.locks calls do nothing.
    drumline: the collection links the drumline service (a drumline_page extension,
    stock Drumline's second page). The shared hooks reach it through
    sdk_drumline_get, defined here: without the service it answers null; with it,
    the service's sdk_drumline_service, which the browser builder resolves to
    kit_absent (null) when the selection leaves the extension out."""
    declarations, screens = [], []
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
    declarations.append('const void* sdk_drumline_get() __asm__("sdk_drumline_get");')
    if drumline:
        declarations.append('const void* sdk_drumline_service() __asm__("sdk_drumline_service");')
        declarations.append('const void* sdk_drumline_get() { return sdk_drumline_service(); }')
    else:
        declarations.append('const void* sdk_drumline_get() { return nullptr; }')
    values = dict(DECLARATIONS='\n'.join(declarations), AUX_STARTUP='\n'.join(screens),
                  ENGINE_SENTINEL=str(ENGINE_SENTINEL), PROJECT_KEPT=kept)
    source = (ROOT / TEMPLATE).read_text(encoding='utf-8')
    for key, text in values.items():
        require(source.count('@' + key + '@') == 1, 'Startup template placeholder missing')
        source = source.replace('@' + key + '@', text)
    require('@' not in source, 'Unexpanded startup template')
    return source


def generate(directory, control=False, aux=False, locks=False, drumline=False):
    destination = Path(directory) / 'startup.cpp'
    destination.write_text(render(control, aux, locks, drumline), encoding='utf-8', newline='\n')
    return destination
