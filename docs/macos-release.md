# Liberty — livraison macOS

Le workflow de diagnostic ARM64 (`.github/workflows/fast-diagnostic.yml`)
produit un DMG utilisable en développement, avec `Liberty.app`, un raccourci
`Applications` et un fichier `.dmg.sha256`. Sa signature **ad-hoc**
n'est **pas** une signature Developer ID ni une notarisation Apple.

Le workflow de production (`.github/workflows/build.yml`) crée un DMG
universel (Apple Silicon + Intel). Quand tous les secrets suivants sont
configurés dans **GitHub → Settings → Secrets and variables → Actions**,
il utilise le certificat Apple Developer ID, le Hardened Runtime,
`notarytool` et `stapler` :

| Secret GitHub Actions | Valeur à fournir |
| --- | --- |
| `MACOS_CERTIFICATE_P12_BASE64` | Certificat **Developer ID Application** et sa clé privée exportés en fichier `.p12`, puis encodés en base64 sur une seule ligne |
| `MACOS_CERTIFICATE_PASSWORD` | Mot de passe du fichier `.p12` |
| `MACOS_SIGNING_IDENTITY` | Nom exact de l'identité, commençant par `Developer ID Application:` |
| `APPLE_NOTARY_KEY_P8_BASE64` | Clé privée App Store Connect API `.p8` encodée en base64 sur une seule ligne |
| `APPLE_NOTARY_KEY_ID` | Identifiant de la clé API |
| `APPLE_NOTARY_ISSUER_ID` | Identifiant de l'émetteur de la clé API d'équipe |

Pour obtenir la valeur base64 sur macOS :
```bash
base64 -i certificate.p12 | tr -d '\n'
base64 -i AuthKey_XXXX.p8 | tr -d '\n'
```

Conserver les fichiers `.p12` et `.p8` hors du dépôt. Ne jamais
les envoyer dans une conversation ou les commiter. Les secrets sont
utilisés uniquement sur les runners GitHub Actions, et les fichiers
temporaires sont supprimés par les scripts.

**Cas possibles :**

- **6 secrets présents :** signature Developer ID avec Hardened Runtime
  et horodatage, création du DMG, validation par Apple, agrafage du ticket,
  contrôle de l'image et création du `.dmg.sha256`.
- **0 secret présent :** build de diagnostic signé ad-hoc, explicitement
  identifié comme **non notarié** dans `RELEASE-STATUS.txt`.
- **Configuration partielle :** arrêt avec erreur plutôt que d'étiqueter
  une version incomplète comme prête à la vente.

Le fichier `RELEASE-STATUS.txt` de l'Artifact indique si la
notarisation a réellement été effectuée. Ne commercialisez pas comme
version notariée une version de diagnostic signée ad-hoc.

Pour vérifier le téléchargement, placer le DMG et son
`.dmg.sha256` dans le même dossier, puis exécuter :

```bash
shasum -a 256 -c Liberty-macOS.dmg.sha256
```

Sur le DMG de diagnostic ARM64, remplacer le nom par
`Liberty-macOS-arm64.dmg.sha256`.

Le certificat Developer ID, l'accès Apple Developer et les autorisations
App Store Connect doivent être mis en place par le propriétaire du compte.
La notarisation ne remplace pas les tests réels sur macOS, notamment
les tests des instruments, plugins et périphériques audio sous Hardened Runtime.
