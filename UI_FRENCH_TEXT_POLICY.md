# Liberty DAW — Règle absolue n° 2 : CARACTÈRES FRANÇAIS INTÈGRES

**Aucun symbole, losange, point d'interrogation ou caractère de remplacement à la place d'une lettre française.** Cette règle est aussi prioritaire que **AUCUN CHEVAUCHEMENT**. Un build ne doit pas être considéré comme validé si un libellé est corrompu.

## Exigences permanentes

- Toutes les sources de l'interface sont enregistrées en UTF-8 valide. Utiliser des caractères français corrects, y compris les majuscules accentuées (É, È, Ê, À, Ç, Œ) ; ne pas supprimer les accents pour contourner un défaut technique.
- Pour les textes JUCE comprenant des accents, employer explicitement `juce::String::fromUTF8(...)` ; lorsqu'un environnement ou un outil altère les octets source, construire le caractère avec `juce::String::charToString(0x....)`.
- Centraliser les libellés sensibles dans `Source/LibertyFrenchLabels.h` ; le bouton DÉTAILS utilise U+00C9 et reste indépendant de l'encodage du fichier C++.
- Rejeter les séquences corrompues habituelles (`Ã©`, `Ã¨`, `â€™`, `�`) et les caractères de contrôle accidentels via `Tests/FrenchTextRegression.py`.
- Avant validation, contrôler visuellement l'affichage dans les boutons, titres, menus, tooltips, listes de plugins, boîtes de dialogue et fenêtres détachées, sur macOS et Windows, avec plusieurs tailles de police et facteurs d'échelle.
- Les tests de fichiers garantissent l'encodage et l'usage du libellé sécurisé, **mais ne peuvent pas vérifier à eux seuls le rendu des polices sur un écran**. Une anomalie visuelle demeure un bug bloquant.

Conserver aussi `UI_NO_OVERLAP_POLICY.md` : **ni caractère corrompu, ni chevauchement, jamais.**
