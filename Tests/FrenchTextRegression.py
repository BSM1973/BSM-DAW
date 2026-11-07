#!/usr/bin/env python3
"""CI: reject mojibake, invalid UTF-8 and regressions in French mixer captions.

Source files must be UTF-8, independent of platform/compiler locale. This
source-level test supplements (not replaces) visual macOS/Windows inspection.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Source"
FILES = sorted(
    path
    for ext in ("*.cpp", "*.h", "*.hpp", "*.mm")
    for path in SOURCE.rglob(ext)
)
ISSUES = []
# Common UTF-8 interpreted as Windows-1252 (or UTF-8 twice), and U+FFFD.
MOJIBAKE = re.compile(r"\uFFFD|ï¿½|Ã[\u0080-\u00bf\u00a0-\u00bf]|Â[\u0080-\u00bf\u00a0-\u00bf]|â€")
C1 = re.compile(r"[\u0080-\u009f]")

if not FILES:
    ISSUES.append("Aucun fichier Source/ trouvé")
for path in FILES:
    try:
        content = path.read_text(encoding="utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        ISSUES.append(f"{path.relative_to(ROOT)} : source non UTF-8 ({exc})")
        continue
    for line_no, line in enumerate(content.splitlines(), start=1):
        if MOJIBAKE.search(line) or C1.search(line):
            ISSUES.append(
                f"{path.relative_to(ROOT)}:{line_no} : caractère corrompu ou mojibake"
            )

mixer_path = SOURCE / "MixConsoleController.cpp"
labels_path = SOURCE / "LibertyFrenchLabels.h"
try:
    mixer = mixer_path.read_text(encoding="utf-8")
    labels = labels_path.read_text(encoding="utf-8")
except OSError as exc:
    ISSUES.append(f"Libellés UI manquants : {exc}")
else:
    # Regression guard: do not put raw accented byte literals directly into
    # JUCE TextButton setters; always use explicit Unicode strings/helpers.
    if re.search(r'detailButton\.setButtonText\s*\(\s*"[^"]*[ÉÈÀÊ]', mixer):
        ISSUES.append("Le bouton DÉTAILS doit utiliser le libellé Unicode centralisé")
    if mixer.count("LibertyFrenchLabels::mixerDetails()") < 2:
        ISSUES.append("Le mode DÉTAILS et son retour doivent utiliser le même libellé")
    if "LibertyFrenchLabels::mixerCompact()" not in mixer:
        ISSUES.append("Libellé COMPACT non raccordé")
    if not re.search(r"charToString\s*\(\s*0x00[Cc]9\s*\)", labels):
        ISSUES.append("La lettre française É (U+00C9) n'est plus garantie")
    if 'juce::String("D")' not in labels or '"TAILS"' not in labels:
        ISSUES.append("Le libellé DÉTAILS n'est plus correctement assemblé")

# The live UI cleaner is a second safety net for legacy garbled text.
# Verify real French letters (including upper case) remain repairable,
# and a valid stand-alone accent is never inadvertently stripped.
cleaner = (SOURCE / "UISymbolCleaner.cpp").read_text(encoding="utf-8")
replacements = {
    int(bad, 16): int(good, 16)
    for bad, good in re.findall(
        r"\\{0x([0-9A-Fa-f]{4}),0x([0-9A-Fa-f]{4})\\}", cleaner
    )
}
for trailing, expected in {
    0x00A9: 0x00E9,  # e acute
    0x00A8: 0x00E8,  # e grave
    0x00EA: None,    # do not rewrite an already correct e circumflex
    0x00A0: 0x00E0,  # a grave
    0x00A7: 0x00E7,  # c cedilla
    0x00B4: 0x00F4,  # o circumflex
    0x2030: 0x00C9,  # E acute: DÉTAILS
    0x02C6: 0x00C8,  # E grave
    0x2021: 0x00C7,  # C cedilla
}.items():
    if expected is not None and replacements.get(trailing) != expected:
        ISSUES.append(
            f"Réparation d'accent absente : 0x{trailing:04X} -> 0x{expected:04X}"
        )
if "if(text.contains(brokenPrefix))" not in cleaner:
    ISSUES.append("Le nettoyeur doit ne modifier que les séquences vraiment corrompues")

if ISSUES:
    print("ÉCHEC : intégrité des caractères français")
    print("\n".join(ISSUES[:60]))
    if len(ISSUES) > 60:
        print(f"... {len(ISSUES)-60} autre(s) problème(s)")
    sys.exit(1)
print(f"OK : {len(FILES)} sources UTF-8, sans mojibake détecté ; DÉTAILS = U+00C9")
