"""Generate the overlay's client list (overlay_clients.cpp): the SDK units that draw
on the SDK's overlay over every page (overlay.hpp), each listed only in builds that
link that unit. A service the overlay doesn't link against adds its client at run
time instead (sdk_overlay_add)."""
from pathlib import Path

from .formats import require

# (unit, client): the unit that defines the client, a const overlay::Client.
CLIENTS = (
    ('cpu_overlay', 'sdk_cpu_overlay'),  # the cpu meter's indicator and breakdown
)


def render(units, clients=CLIENTS):
    """units: the SDK units the build links."""
    linked = [client for unit, client in clients if unit in units]
    require(all(client.isidentifier() for client in linked), 'Invalid overlay client')
    lines = ['// Generated: the overlay\'s linked clients (tooling/overlay_clients.py).',
             '#include "overlay.hpp"', '', 'using opxy::platform::overlay::Client;', '']
    if linked:
        lines += ['extern "C" {']
        lines += [f'extern const Client {client} __asm__("{client}");' for client in linked]
        lines += ['}', '']
    lines.append('const Client* const sdk_overlay_clients[] = {'
                 + ''.join(f'&{client}, ' for client in linked) + 'nullptr};')
    return '\n'.join(lines) + '\n'


def generate(directory, units):
    destination = Path(directory) / 'overlay_clients.cpp'
    destination.write_text(render(units), encoding='utf-8', newline='\n')
    return destination
