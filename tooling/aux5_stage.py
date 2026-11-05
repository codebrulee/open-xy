"""Generate Aux 5's stage (aux5_stage.s): what runs at the mixer's call of Aux 5's
audio processor. The SDK owns that site once; features reach it here instead of
hooking it themselves. Each slot names the SDK unit that provides it and its
entry point, and is filled only in builds that link that unit."""
from pathlib import Path

from .definitions import ROOT
from .formats import require

TEMPLATE = 'sdk/platform/opxy-1.2.1/aux5_stage.cpp.in'

# An effect on the aux bus before Aux 5's processor:
# void entry(u32 a, u32 b, const i32* bus), with the processor's R0 and R1.
EFFECT = None


# A timer around the effect and the processor: void entry(u32 start, u32 end), CYCLES.
TIMER = None


def render(units, effect=None, timer=None):
    """units: the SDK units the build links."""
    declarations, bodies = [], {}
    for key, slot, signature, call in (
            ('EFFECT', effect, '(u32, u32, const i32*)', '(a, b, bus)'),
            ('TIMER', timer, '(u32, u32)', '(start, end)')):
        if slot is None or slot[0] not in units:
            bodies[key] = ''
            continue
        unit, entry = slot
        require(entry.isidentifier(), 'Invalid aux 5 stage entry point')
        declarations.append(f'void {entry}{signature} __asm__("{entry}");')
        bodies[key] = f'    {entry}{call};'
    source = (ROOT / TEMPLATE).read_text(encoding='utf-8')
    for key, text in dict(DECLARATIONS='\n'.join(declarations), **bodies).items():
        require(source.count('@' + key + '@') == 1, 'Aux 5 stage template placeholder missing')
        source = source.replace('@' + key + '@', text)
    require('@' not in source, 'Unexpanded aux 5 stage template')
    return source


def generate(directory, units):
    destination = Path(directory) / 'aux5_stage.cpp'
    destination.write_text(render(units, EFFECT, TIMER), encoding='utf-8', newline='\n')
    return destination
