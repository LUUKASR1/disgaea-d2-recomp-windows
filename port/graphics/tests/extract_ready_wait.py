"""Test the actual adapted Win32 wait function, rather than a copied guard."""
from pathlib import Path
import re
import sys
source = Path(sys.argv[1]).read_text(encoding='utf-8')
functions = re.findall(r'^void cellGcm_fifo_kick_wait\(unsigned ms\)\n\{.*?^\}', source, re.M | re.S)
if len(functions) != 1 or functions[0].count('d2_profiled_fifo_wait(kick, ms)') != 1:
    raise RuntimeError('Adapted wait function changed')
Path(sys.argv[2]).write_text(functions[0] + '\n', encoding='utf-8')
