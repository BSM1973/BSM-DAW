# Liberty DAW — trois éditions natives

Inventaire de référence : boutique Liberty Music Software, 24 novembre 2026,
22 produits actifs dont Liberty DAW et 21 plugins.

## Les éditions

Liberty DAW (Base) conserve exactement le DAW actuel, y compris son moteur
audio, Drum Studio, les instruments/effets déjà natifs et l'hébergement
optionnel de plugins tiers AU/VST3.

Liberty DAW Standard ajoute DEUX instruments et TROIS effets LIBERTY NATIFS.
Sélection technique provisoire, issue des dépôts C++ accessibles :
- Instruments : ECHO 86 et FM Emancipation.
- Effets : FreeSpirit Delay, LOVE EQ et PEDAL LEGENDS.

Liberty DAW Ultimate ajoute tous les 21 produits plugins du catalogue,
soit 7 instruments et 14 effets. Les futurs plugins ne doivent pas être
insérés silencieusement sans être validés et ajoutés au catalogue.

La proposition commerciale alternative FREEDOM + ECHO 86 et FreeSpirit +
Horizon Reverb + LOVE EQ reste intéressante, mais les sources complètes
de FREEDOM et Horizon Reverb ne sont pas accessibles dans les dépôts
actuellement identifiés. Elles ne peuvent pas être intégrées par illusion
avec les fichiers AU/VST3 : remplacer la sélection après accès aux DSP.

## Native signifie directement compilé dans Liberty

Les vrais effets et instruments se créent via des fabriques C++ retournant
des juce::AudioProcessor, et non via AudioPluginInstance, le scanner VST3,
le chargeur AU ou des chemins vers des bundles externes. Les fonctions
globales createPluginFilter() des projets standalone doivent être isolées
pour éviter les collisions lors de l'édition de liens.

Les mêmes moteurs peuvent exister en deux produits :
- appareil intégré dans Liberty, utilisable uniquement depuis Liberty;
- version AU/VST3 autonome, achetée séparément sur la boutique pour
  utilisation dans Ableton, Logic, Cubase, Bitwig, etc.

Attention : le binaire peut toujours être analysé techniquement. Le
conditionnement natif seul ne constitue pas une DRM inviolable.

## Politique d'activation et de projet

- Les identifiants stables commencent par liberty.native. et ne sont pas
  les ID du plugin AU/VST3. Aucun système ne doit scanner des AU/VST3 pour
  activer un appareil livré dans une édition.
- Le catalogue indique les appareils ENVISAGÉS. Le navigateur ne peut
  montrer un natif que si NativeDeviceRegistry::isAvailable retourne vrai,
  c'est-à-dire si une vraie fabrique DSP est liée ET l'édition l'autorise.
- Toute sauvegarde de projet avec appareil absent de l'édition doit
  ultérieurement conserver son état, paramètres et automatisations sans
  pertes, avec signalement explicite d'appareil manquant; ne pas mettre un
  bouton fictif à sa place. Les autres pistes doivent rester lisibles.
- Un achat de version AU/VST3 n'est pas automatiquement une licence
  d'édition et vice versa, sauf offre commerciale explicitement décidée.
- Ne pas réutiliser les wrappers JUCE VST3/AU ni générer ces formats depuis
  le projet DAW. Les produits de boutique indépendants restent inchangés.
- Garder sur toutes les éditions le support des plugins tiers AU/VST3.

## Exigences de publication

Trois identités produit, trois installateurs et trois signatures/notarisations.
Même coeur DAW; natifs compilés et disponibles selon l'édition.
Vérifier licences JUCE, dépendances tierces, IR et banques samples pour les
différents modes de distribution.

Pour chaque instrument ou effet porté : identité sonore vs version boutique,
presets, états sauvegardés, automation, MIDI/MPE lorsque pertinent,
latence et compensation, offline bounce, CPU et stabilité. Interface française
par défaut, 13 pouces, zéro chevauchement, sans ascenseurs.

## État réel de cette étape

- Catalogue de 21 plugins et sélection Standard (2+3) codés et testés.
- Contrat de registre natif présent, sans fabrique DSP inventée.
- Les cinq moteurs Standard ne sont PAS encore intégrés nativement.
- Standard et Ultimate ne sont donc PAS encore des versions distribuables.
- Le build Base actuel reste l'unique édition installable. Les demandes
  CMake STANDARD et ULTIMATE doivent échouer explicitement tant que les
  moteurs et ressources de chaque produit ne sont pas réellement compilés.

Prochaine étape : porter ECHO 86 et FreeSpirit Delay en natif, avec les
moteurs UI/presets, puis reprendre le routage d'insertion dans Liberty.
Intégrer ensuite les autres produits et préparer trois DMG authentiques,
sans perte de fonctions de l'édition Base.
