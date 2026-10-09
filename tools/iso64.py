#!/usr/bin/env python3
"""Package an already built x86-64 kernel as a GRUB ISO."""
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/x86_64'
boot = OUT/'isodir/boot'
(boot/'grub').mkdir(parents=True, exist_ok=True)
shutil.copy2(OUT/'myos.kernel', boot/'myos.kernel')
(boot/'grub/grub.cfg').write_text('menuentry "ZenOS x86-64" {\n multiboot /boot/myos.kernel\n}\n')
subprocess.run(['grub-mkrescue', '-o', str(OUT/'myos.iso'), str(OUT/'isodir')], check=True)
