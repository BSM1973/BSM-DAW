# Liberty DAW — Step Sequencer MIDI : référence vidéo du 19 novembre 2026

## Statut et référence
Source étudiée : vidéo utilisateur `ScreenFlow(20261119-144157).mp4`, 90,13 secondes, 1920 × 1080, 30 i/s. Il s'agit du Step Sequencer mélodique affiché dans Ableton Live. L'enregistrement audio est pratiquement silencieux : aucun jugement sur la qualité sonore ni sur la fidélité du moteur MIDI ne peut être tiré de cette vidéo. Cette spécification décrit des comportements visuellement vérifiables, puis des choix de développement proposés ; ceux-ci sont distincts.

**Directive produit :** reproduire la structure, la rapidité et les interactions de l'éditeur montré, sans reprendre les marques/éléments propriétaires d'Ableton. Utiliser le design Liberty bleu nuit, français accentué, aucun caractère corrompu, aucun chevauchement. Conserver le fonctionnement de toutes les autres pages de Liberty DAW.

## Relevé chronologique de la vidéo

| Horodatage | Ce qui est visible |
|---|---|
| 00:00–00:06 | Séquence MIDI initialement vide ; 8 lignes de hauteurs C4, B3, A3, G3, F3, E3, D3, C3 ; grille de 16 pas ; premiers clics activant des notes orange. |
| 00:06–00:24 | Construction de la figure : activation et retrait de cases sur plusieurs hauteurs et positions ; distinction visible entre cases inactives grises et notes orange. |
| 00:24–00:35 | Éditions de positions et de durées ; la note E3 couvre plusieurs colonnes, contrairement à une case ON/OFF. |
| 00:35–00:54 | Programmation d'une suite de notes adjacentes sur C3, sélection multiple (contours cyan), onglet de propriétés Pitch affichant +3.00 st pour plusieurs notes sélectionnées. |
| 00:54–01:06 | Interaction avec les contrôles par ligne (dont boutons d'écoute / solo) ; affichage temporairement désactivé/grisé. Ne pas déduire le type exact d'état à partir de l'image seule. |
| 01:06–01:30 | Réglages par ligne Steps, Rate, Dir, Swing ; un Swing à 100 % apparaît sur la première ligne ; nouvelle sélection/édition des cellules. |

## Interface constatée

1. Partie clip à gauche : taille `16 Steps`, `Loop Start`, `Loop Length`, Loop, Duplicate, signature 4/4, Groove et tonalité/gamme.
2. Partie édition au centre : onglets `Steps` et `Envelopes` ; colonnes temporelles numérotées 1, 5, 9, 13.
3. Huit lignes de hauteurs musicales, non limitées aux percussions : couleurs de bandeau, nom de note, deux commandes compactes d'écoute et de solo.
4. Quatre paramètres indépendants par ligne : `Steps` (16 affiché), `Rate` (1/16 affiché), `Dir` (flèche), `Swing` (0 % à l'état initial, valeur 100 % visible en fin de vidéo sur C4).
5. Notes actives orange, étapes vides gris foncé, sélection cyan, étape/événement en focus visuellement différencié.
6. Longues notes dessinées horizontalement sur plusieurs pas ; notes successives contiguës conservées comme événements distincts.
7. En bas, éditeurs `Velocity`, `Offset`, `Length`, `Repeat`, `Pitch`, `Chance`. La vidéo montre une édition groupée de Pitch avec valeur +3.00 st ; les autres onglets sont visibles mais leurs gestes ne sont pas tous démontrés.
8. La grille est visible simultanément à la zone principale du logiciel et au panneau clip ; ne pas remplacer toute l'application par une fenêtre isolée.

## Comparaison avec l'implémentation actuelle (GitHub, branche fix/midi-timeline-instrument-audio-32aa5ab)

- Existant : `Source/StepSequencer.h/.cpp` avec 64 pas maximum, huit banques de patterns par piste, notes MIDI exportables, vitesse/velocity, gate, probability, ratchet, tie, accent, microtiming, swing, direction, gammes et XML de projet.
- Existant : `MainComponent.cpp` fournit un Step Sequencer MIDI sur une **seule rangée temporelle de 16 pas** avec pages 16/32/48/64, ainsi qu'une vue instrument indépendante de 16 pas et un éditeur détachable.
- **Écart structurel majeur :** une seule valeur `pitch` par `Step` et par colonne ; la vidéo montre plusieurs hauteurs simultanées dans une grille multi-lignes, chacune avec ses propres réglages.
- **Autres écarts :** édition par glissement et redimensionnement de note longue, sélection de plusieurs événements, éditeur de propriétés contextualisé à la sélection, mute/solo/audition par hauteur, réglages par ligne et lisibilité d'une grille huit lignes dans le dock.
- Ne pas prétendre que ces fonctionnalités sont présentes parce que leurs paramètres existent dans le moteur : une fonctionnalité non pilotable et non audible depuis la nouvelle UI n'est pas terminée.

## Architecture cible proposée (non démontrée directement dans la vidéo)

- Un **modèle de clip pas-à-pas polyphonique** distinct du modèle historique monophonique par colonne : notes avec identifiants stables, `laneId`, note MIDI, `startTick`, `lengthTicks`, `velocity`, `offset`, `repeat`, `pitchOffset`, `chance`, canal et mute. Ne pas utiliser la couleur ni le rectangle UI comme source de vérité.
- Paramètres par ligne : note MIDI, activation/mute, solo, steps, rate (numérateur/dénominateur rationnels MIDI), direction, swing, phase/longueur de cycle. Des longueurs différentes par ligne doivent pouvoir fonctionner sans perte de synchronisation.
- Transport : ticks MIDI (960 PPQ existants) et détection exacte des frontières de boucles ; gestion des Note On/Off, notes longues, répétitions, changement de tempo, bouclage et arrêt. Zéro allocation, verrou bloquant ou E/S disque depuis le callback audio.
- UI JUCE : composant natif séparé pour la grille, outil click/drag, gestion visible du survol et des sélections, invalidation locale uniquement, pas de rafraîchissement global dépendant d'un timer pour la géométrie.
- Compatibilité : conserver les banques et anciens projets via un adaptateur ou une migration de données versionnée. Ne pas écraser la définition historique `Pattern` avant de vérifier son usage par tous les éditeurs.
- Undo/Redo transactionnels pour chaque geste souris ; synchronisation avec les clips de l'arrangement et le Piano Roll ; aucune note fantôme/doublée.

## Ordre de développement et critères de validation

### Phase 1 — modèle MIDI et compatibilité
- Ajouter types de pistes de notes/lignes et événements polyphoniques indépendants.
- Tests sans GUI : notes simultanées, note de trois pas, notes contiguës, mute/solo, rate/swing par ligne, bouclage, persistence et migration.
- Ne pas altérer le moteur existant tant que la compatibilité n'est pas validée.

### Phase 2 — UI identique dans sa logique
- Grille 8 lignes × 16 pas par défaut, hauteur nommée, bandeaux colorés, commandes de ligne, contrôle Steps/Rate/Dir/Swing, panneau clip et six onglets d'édition en bas.
- Fenêtre complète dès l'ouverture sur écran 13 pouces, disposition sans superposition, texte français UTF-8, pictogrammes vectoriels.
- Pan/zoom/scroll uniquement si réellement nécessaire, sans cacher les fonctions essentielles.

### Phase 3 — gestes d'édition
- Cliquer pour créer/retirer, glisser pour peindre ou déplacer, attraper le bord pour changer une durée, Ctrl/Cmd et Shift pour sélectionner, modifier plusieurs événements à la fois.
- Six panneaux de propriétés câblés à des données MIDI réelles ; valeurs en cellule quand le mode le requiert (ex. Pitch +3 st).
- Undo/Redo de chaque opération, raccourcis et cohérence sélection.

### Phase 4 — lecture et projet
- Préécoute/écoute par ligne et mute/solo, transport et émission MIDI vers la piste choisie, enregistrement et réouverture de projet.
- Synchronisation de la grille avec les clips MIDI de Liberty et le Piano Roll ; tests en changement de tempo et sur boucles.
- Non-régression Drum Sampler / instruments / autres pages.

### Phase 5 — validation qualité
- Tests unitaires de rendu, tests JUCE des hitboxes, captures à plusieurs résolutions dont 13 pouces, tests réels de lecture MIDI/AU/VST3.
- Vérification systématique : aucun chevauchement, aucun symbole substitué aux lettres, pas de changements de notes impossibles à annuler.
- Publier un artefact macOS ARM64 seulement après tests verts, sans annoncer comme « fonctionnel » un écran uniquement dessiné.

## Définition de terminé

Sur une piste Instrument MIDI, l'utilisateur peut créer le motif correspondant à celui de la vidéo (huit hauteurs, mélodie C4–D3, note E3 de plusieurs cellules, ligne C3 avec événements contigus), modifier une sélection multiple dans le panneau Pitch, utiliser des Steps/Rate/Dir/Swing différents sur deux lignes, entendre le résultat, créer un clip de l'arrangement, fermer/rouvrir Liberty et retrouver le motif à l'identique. Aucun doublon MIDI, aucun chevauchement graphique, aucun caractère français cassé.
