# Liberty DAW — Priorité absolue : AUCUN CHEVAUCHEMENT

Cette règle est prioritaire sur toute nouvelle fonctionnalité ou modification de l'interface.

- Aucun bouton, menu, libellé, titre, fader, ascenseur, fenêtre, panneau, popup ou zone interactive ne doit occuper la même zone qu'un autre élément qui doit rester visible ou utilisable.
- Réserver des rectangles explicites aux différents modules; ne pas résoudre une collision en masquant un élément utile derrière une autre couche.
- Si l'espace manque, replier, déplacer, redimensionner, faire défiler ou proposer un panneau latéral dédié.
- Un menu de routage doit rester rattaché à sa piste et ne jamais se présenter par défaut au-dessus du logo ou des boutons de création.
- Préserver les dimensions et les positions des contrôles de mixage lors de l'ajout de fonctions.
- Tester le placement avec plusieurs tailles de fenêtres, les pistes réordonnées, les panneaux ouverts et fermés, et la navigation au clavier.
- Une régression géométrique détectée bloque la validation du build, avant toute autre fonctionnalité.

Le code partagé `Source/NoOverlapLayout.h` définit les zones réservées; `Tests/NoOverlapLayoutRegression.cpp` vérifie les invariants sans dépendance JUCE. Ces tests ne remplacent pas une vérification visuelle sur macOS et Windows : les popups système, les polices et les facteurs d'échelle doivent aussi être examinés.
