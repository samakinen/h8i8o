# SPDX-License-Identifier: GPL-3.0-or-later
"""Remove the MasterData element from the exported product XML.

Packers that sign with the ETS libraries (OpenKNXproducer) copy MasterData
into every part of the archive, and ETS's hasher then fails with "Naming pair
for MasterData cannot be found". The rest of the file is left byte for byte.

    python tools/knxprod/strip_masterdata.py build/knxprod/h8i8o.knxprod.xml build/knxprod/h8i8o.ets.xml
"""

import re
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as source:
        text = source.read()
    stripped, count = re.subn(r"[ \t]*<MasterData\b.*?</MasterData>[ \t]*\r?\n?", "", text, flags=re.DOTALL)
    if count != 1:
        print(f"error: expected one MasterData element, found {count}", file=sys.stderr)
        return 1
    with open(sys.argv[2], "w", encoding="utf-8", newline="") as target:
        target.write(stripped)
    return 0


if __name__ == "__main__":
    sys.exit(main())
