#!/usr/bin/env python3
"""Create an x86-64 development disk using the shared disk builder."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tools/disk64.py'), run_name='__main__')
