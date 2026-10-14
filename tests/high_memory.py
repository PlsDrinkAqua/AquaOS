#!/usr/bin/env python3
"""Force allocations above 4 GiB; validate page tables, fork and E1000 DMA."""
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
env = dict(os.environ)
env['ZENOS64_CC'] = env.get('ZENOS64_CC', 'gcc') + ' -DPMM_HIGH_TEST'
env['AQUAOS_TEST_MEMORY'] = '5G'
env['AQUAOS_TEST_HIGH_RAM'] = '1'
try:
    subprocess.run(['python3', 'tools/build64.py'], cwd=ROOT, env=env, check=True)
    subprocess.run(['python3', 'tools/iso64.py'], cwd=ROOT, check=True)
    subprocess.run(['python3', 'tests/network_smoke.py'], cwd=ROOT, env=env, check=True)
finally:
    subprocess.run(['python3', 'tools/build64.py'], cwd=ROOT, check=True)
    subprocess.run(['python3', 'tools/iso64.py'], cwd=ROOT, check=True)
