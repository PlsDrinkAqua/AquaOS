"""Physical address of the framebuffer terminal character mirror."""
import subprocess
from functools import lru_cache
from pathlib import Path

@lru_cache(maxsize=1)
def console_address():
    kernel = Path(__file__).resolve().parents[1] / 'build/x86_64/aquaos.kernel'
    for line in subprocess.check_output(['nm', '-n', str(kernel)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] == 'terminal_screen':
            return hex(int(fields[0], 16) - 0xffffffff80000000)
    raise RuntimeError('terminal_screen symbol missing')
