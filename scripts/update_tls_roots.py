"""Regenerate trusted GitHub roots from the installed Mozilla/certifi bundle."""
import re
from pathlib import Path

import certifi

ROOTS = {"ISRG Root X1", "DigiCert Global Root G2", "Sectigo Public Server Authentication Root R46",
         "Sectigo Public Server Authentication Root E46"}


def main():
    bundle = Path(certifi.where()).read_text(encoding="ascii")
    certificates = []
    found = set()
    for comments, pem in re.findall(r"((?:#[^\n]*\n)+)(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)", bundle, re.S):
        label = re.search(r'# Label: "([^"]+)"', comments)
        if label and label[1] in ROOTS:
            certificates.append(pem)
            found.add(label[1])
    if found != ROOTS:
        raise RuntimeError(f"Missing trust roots: {ROOTS - found}")
    target = Path(__file__).resolve().parents[1] / "firmware/include/tls_roots.h"
    target.write_text('#pragma once\n// Mozilla trust roots extracted from certifi; see scripts/update_tls_roots.py.\n'
                      'static const char TLS_ROOTS[] PROGMEM = R"PEM(\n' + '\n'.join(certificates) + '\n)PEM";\n', encoding="ascii")
    print(f"Wrote {len(found)} trust roots to {target.name}")


if __name__ == "__main__":
    main()
