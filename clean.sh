#!/bin/sh
set -eu
cd "$(dirname "$0")"
python3 - <<'PYTHON'
from pathlib import Path
import shutil
output = Path('build/x86_64')
if output.exists(): shutil.rmtree(output)
PYTHON
