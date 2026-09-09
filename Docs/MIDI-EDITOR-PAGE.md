# Liberty — MIDI Editor

## Page MIDI 1

Cette page décrit l’éditeur MIDI de Liberty et son comportement d’édition.

### Édition des notes

- Un clic sur une note la sélectionne.
- Un double-clic dans une zone vide crée une nouvelle note.
- La note sélectionnée est clairement mise en évidence.
- Une note peut être déplacée horizontalement et verticalement.
- Le déplacement suit la grille MIDI de 1/16.
- Les bords gauche et droit permettent de modifier la durée de la note.
- La vélocité est éditable dans la lane `VELOCITY`.

### Raccourcis

- `⌘/Ctrl + C` : copier la note sélectionnée.
- `⌘/Ctrl + V` : coller la note copiée.
- `⌘/Ctrl + D` : dupliquer la note sélectionnée.
- `Delete` / `Backspace` : supprimer la note sélectionnée.

### Principes d’interface

- Aucune création de note accidentelle par simple clic dans le vide.
- Les actions destructives sont explicites.
- La sélection reste indépendante du focus visuel de la fenêtre native.
- L’éditeur conserve une géométrie déterministe afin d’éviter tout chevauchement des contrôles.

### Étape suivante

La prochaine évolution prévue est l’édition MIDI multi-sélection : sélection de plusieurs notes, déplacement groupé, duplication groupée et suppression groupée, avant l’introduction de l’undo/redo MIDI.
