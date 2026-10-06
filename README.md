# Mini-journal thermique

Un journal matinal autonome pour **Phomemo M02 Pro et ESP32 original**. GitHub Actions récupère les actualités, une API IA gratuite sélectionne et résume les nouvelles, puis l’ESP32 imprime le ticket lorsque l’imprimante est allumée. Le PC peut rester éteint.

Le ticket contient une salutation personnalisable, la date, **FRANCE**, **MONDE** et exactement une information **TECH**. Noir et blanc, titres hiérarchisés, séparateurs, sans météo, icônes ni sources affichées. La longueur suit le texte, avec un maximum d’environ 20 cm.

## Fonctionnement

```text
GitHub Actions → RSS → OpenRouter gratuit → sélection → rédaction → relecture/corrections
              → mise en page → branche publique journal
                                      ↓ HTTPS
                            ESP32 : cache en flash
                                      ↓ Bluetooth Classic SPP
                              Phomemo M02 Pro
```

Le cycle démarre à **05:00, heure de Paris**, été comme hiver. Un ESP32 alimenté plus tard rattrape le cycle au démarrage. Il consulte le manifeste toutes les cinq minutes (une minute s’il n’est pas encore disponible), puis conserve le Bluetooth actif une fois l’édition du jour reçue. Une connexion refusée trois fois redémarre le Bluetooth, sans redémarrer l’ESP32. Le Wi-Fi est suspendu pendant l’envoi puis reste coupé jusqu’au cycle suivant. Une date mémorisée empêche les doubles impressions, même après redémarrage.

## Matériel

- ESP32 original avec Bluetooth Classic, par exemple **ESP-WROOM-32D**, flash 4 Mo.
- Phomemo M02 Pro chargée et papier continu.
- Wi-Fi 2,4 GHz et alimentation USB pour l’ESP32.

CH340 désigne l’interface USB. Un ESP8266 ou un ESP32 sans Bluetooth Classic ne peut pas utiliser ce firmware. Fermer l’application Phomemo si elle monopolise la connexion.

## Installer sur GitHub

1. Copier ou forker le dépôt dans un dépôt **public** et activer GitHub Actions.
2. Dans **Settings → Secrets and variables → Actions**, ajouter le secret `OPENROUTER_API_KEY`.
3. Facultatif : dans l’onglet **Variables**, définir `JOURNAL_GREETING`, par exemple `Bonjour Camille.`. Sans variable, la salutation vient de `config.json` et vaut `Bonjour.`.
4. Lancer **Actions → Journal quotidien → Run workflow**.
5. Vérifier l’aperçu dans les artifacts et le manifeste public :

```text
https://raw.githubusercontent.com/TON_COMPTE/mini-journal-thermique/refs/heads/journal/manifest.json
```

Le workflow publie uniquement une édition rédigée par OpenRouter et validée. En cas d’échec, aucune nouvelle édition n’est publiée ; les rattrapages pourront réessayer. La branche `journal` contient uniquement la dernière édition. Les clés API restent dans les secrets GitHub ; l’ESP32 n’en a pas besoin.

Le mode manuel permet de forcer une nouvelle édition, de **décocher la publication pour tester sans imprimer**. L’IA est obligatoire dans tous les cas. Le test utilise le même générateur que le journal quotidien.

## Horaires et configuration

Le workflow est programmé à **05:00 Europe/Paris**, avec des rattrapages à :17 et :47 jusqu’à 12:47. Le champ `timezone` suit automatiquement l’heure d’été et d’hiver. Une édition déjà publiée pour la date du jour arrête les tentatives suivantes avant installation des dépendances et appel IA. Les horaires se modifient dans `.github/workflows/journal.yml` et `DAILY_START_HOUR` côté ESP32.

GitHub peut retarder ou manquer un lancement planifié. Pour éviter de dépendre de ce cron, l’ESP32 peut envoyer directement un `workflow_dispatch` à 05:00 si l’édition du jour manque. Si elle reste indisponible, il redemande la génération au maximum toutes les dix minutes ; cette limite survit au redémarrage. Le workflow conserve sa garde de date et ne régénère pas une édition déjà publiée. Ce déclenchement direct ne garantit pas la disponibilité du réseau, du runner ou de l’imprimante.

Pour l’activer, créer un **fine-grained personal access token** GitHub limité au seul dépôt du journal, avec **Actions : Read and write** (Metadata en lecture est automatique). Copier sa valeur dans `GITHUB_ACTIONS_TOKEN` dans le fichier local ignoré `firmware/include/journal_config.h`, puis téléverser le firmware. Aucun droit Contents en écriture n’est requis pour ce jeton. Vérifier sa date d’expiration et le remplacer avant échéance. Ne jamais le committer ni publier les binaires compilés avec cette configuration. Sans jeton, le cron reste le seul déclencheur.

Les workflows planifiés des dépôts publics peuvent être désactivés après une période d’inactivité : vérifier Actions si la génération cesse.

`config.json` contient les flux, le fuseau, l’âge maximal des nouvelles, les modèles et la mise en page. Par défaut : sept flux, nouvelles des dernières 36 heures, neuf candidats maximum par rubrique. Les sources sans date ou indisponibles sont ignorées ; chaque rubrique doit conserver au moins un candidat.

### Génération du texte

OpenRouter est le seul fournisseur. Le modèle initial est `nvidia/nemotron-3-super-120b-a12b:free`, validé sur une génération réelle. Le script accepte uniquement `openrouter/free` ou un identifiant se terminant par `:free`. Aucun modèle payant ni secours RSS brut n’est utilisé. Le routeur gratuit sert de reprise en cas d’échec.

1. **Sélection** : l’IA choisit seulement les identifiants des nouvelles importantes, sans doublons entre rubriques. Elle vise deux informations FRANCE, deux MONDE et exactement une TECH ; une troisième nouvelle générale est possible si elle est essentielle.
2. **Rédaction** : seuls les candidats retenus sont transmis pour produire des titres précis et des phrases complètes, fondées sur leurs extraits. Cibles : 50 caractères par titre, 180 par résumé ; limites finales : 65 et 240 caractères, 220 mots pour toute l’édition.
3. **Relecture** : un appel distinct vérifie les faits à partir des candidats, le français, les répétitions et les longueurs, sans changer les identifiants ni leur ordre. Une relecture inutilisable ne remplace pas un article déjà valide. Si la relecture est indisponible, le brouillon IA reste utilisable seulement s’il passe les contrôles locaux ; cette dégradation est signalée dans les diagnostics.
4. **Corrections ciblées** : seuls les articles ayant un défaut sont reformulés, avec le motif et les longueurs mesurées. Les autres textes sont conservés. Jusqu’à deux passes sont possibles. Si toute l’édition dépasse 220 mots, ses textes sont condensés en gardant la sélection.
5. **Validation et publication** : chaque rubrique, identifiant, longueur et fin de phrase est contrôlé. Aucun texte n’est coupé pour faire passer la validation. Une édition encore invalide échoue et ne remplace pas le journal publié.

L’identifiant gratuit effectivement retourné est conservé pour les étapes suivantes. Une première interruption réseau est réessayée avec ce même modèle et le schéma strict. Les autres échecs peuvent revenir au routeur gratuit et à un mode JSON compatible, avec le schéma complet dans la consigne et une validation locale. Une correction dispose de trois tentatives au maximum, les autres étapes de deux ; les deux passes de correction restent distinctes de ces reprises. Les budgets globaux se règlent dans `config.json` : **16 384 tokens** par appel, **32 768** en reprise, **8 appels maximum** et **480 secondes** pour l’ensemble de la génération. Le raisonnement demandé est faible, sans le désactiver pour les modèles qui l’imposent. Les tokens de raisonnement partagent généralement le budget de sortie ; plus de tokens n’allongent pas le texte imprimé.

Le gratuit reste soumis à la disponibilité et aux quotas OpenRouter. Une erreur d’authentification, de crédit ou de quota arrête les appels de l’étape plutôt que de multiplier les essais immédiats. Une journée réussie utilise normalement trois appels, puis la garde de date évite les générations suivantes. Les rattrapages d’une journée en échec peuvent consommer d’autres appels.

L’artifact `out/ai-diagnostics.json` conserve les étapes, modèles, motifs de rejet, tokens de sortie et de raisonnement lorsqu’ils sont fournis par l’API. Il est disponible même après un échec de génération. Les clés et réponses brutes ne sont pas enregistrées. Le contrôle automatique ne garantit pas l’exactitude de chaque reformulation : la richesse du journal reste limitée aux extraits RSS fournis.

## Configurer l’ESP32

Installer Python 3.12 et les outils :

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
```

Copier `firmware/include/config.example.h` vers `firmware/include/journal_config.h` et renseigner le Wi-Fi, l’URL publique avec son `/` final et l’adresse Bluetooth de l’imprimante. Ce fichier local est ignoré par Git. Une MAC vide utilise le nom exact `M02 Pro` ; `SCAN` permet de trouver l’adresse. Le canal SPP est découvert automatiquement.

```powershell
.\.venv\Scripts\pio.exe device list
.\.venv\Scripts\pio.exe run -d firmware
.\.venv\Scripts\pio.exe run -d firmware -t upload --upload-port COM5
```

Remplacer `COM5` par le port détecté. Si nécessaire, maintenir BOOT et appuyer brièvement sur EN/RESET pour entrer en mode programmation.

Au **premier démarrage seulement**, initialiser LittleFS :

```powershell
.\.venv\Scripts\pio.exe run -d firmware -t uploadfs --upload-port COM5
.\.venv\Scripts\pio.exe device monitor --port COM5 --baud 115200
```

Une mise à jour normale nécessite uniquement `upload` : `uploadfs` remplace le cache. La partition NVS conserve les réglages et l’anti-doublon. La carte utilise une application de 3 Mo et environ 1 Mo de LittleFS, sans OTA.

## Tester

```powershell
.\.venv\Scripts\python.exe -m pytest -q
.\.venv\Scripts\python.exe -m mini_journal --demo
.\.venv\Scripts\python.exe -m mini_journal --check-feeds
```

`out/preview.png` montre le résultat. La démo est fictive, hors ligne et ne peut pas être publiée par le script. Pour tester l’IA localement, définir `OPENROUTER_API_KEY` dans l’environnement puis lancer `python -m mini_journal`.

Pour valider l’installation : laisser l’imprimante éteinte pendant le téléchargement, vérifier `STATUS`, puis l’allumer. Le journal doit sortir complet. Un deuxième allumage et un redémarrage de l’ESP32 ne doivent pas réimprimer la même journée. Terminer par un essai avec l’ESP32 alimenté indépendamment du PC.

| Commande série | Effet |
|---|---|
| `STATUS` | Date, cache, Wi-Fi, impression, éventuel envoi incertain |
| `GENERATE` | Demande directement le workflow GitHub ; conserve le cache et l’anti-doublon |
| `FETCH` | Réactive le Wi-Fi et vérifie le journal immédiatement |
| `SCAN` | Recherche Bluetooth Classic des appareils M02 |
| `TEST` | Petit ticket de contrôle, sans modifier l’anti-doublon |
| `DENSITY 0..4` | Mémorise la chauffe ; 0 laisse le réglage de l’imprimante |
| `NET` | Diagnostic Wi-Fi, DNS et connexion réseau |
| `RETRY` | Autorise un nouvel essai après un envoi incertain |
| `REPRINT` | Télécharge à nouveau puis réimprime l’édition du jour |
| `PRINTAT <timestamp>` | Programme un unique cycle de test dans les prochaines 24 heures |

Un envoi interrompu bloque les nouvelles tentatives **pour la journée concernée**, afin de ne pas sortir plusieurs tickets partiels. Vérifier le papier avant `RETRY`. Une ancienne incertitude est levée le jour suivant. Après redémarrage, une heure Internet valide est nécessaire avant toute impression.

## Format et transport

Le générateur produit un canevas de **626 pixels**, limité à 2362 pixels de haut. La hauteur est ajustée au contenu et les phrases ne sont pas coupées pour remplir la page. Si nécessaire, la taille des caractères diminue, puis les dernières nouvelles générales sont retirées en conservant chaque rubrique.

Le fichier cache conserve son format historique en bandes. Le firmware adapte horizontalement les 626 pixels à **576 points** et transmet **une seule image raster continue**. Après connexion, il attend **2 secondes de stabilité**, transmet séparément les commandes d’initialisation et de densité, attend leur confirmation Bluetooth, puis laisse **300 ms** avant l’image. Cette préparation ne certifie pas un état matériel « prête » ; sa durée doit être validée sur l’imprimante lors de la reprise des tests.

Les paquets SPP de 330 octets passent par la file de transmission BluetoothSerial. Chaque envoi est confirmé et respecte la congestion. Le raster est limité à **3 000 octets/s** : deux départs de paquets pleins sont espacés d’au moins 110 ms, sans rattraper le retard par une rafale après un blocage. Un ACK Bluetooth confirme la réception radio, pas la consommation par le mécanisme d’impression. Écriture, congestion, fermeture et réponses sont filtrées par l’identifiant de connexion ; les confirmations inattendues ne créditent pas les compteurs. La réponse de fin d’image est acceptée seulement après le début de l’envoi du dernier paquet de l’image, puis l’avance papier est transmise avant de mémoriser l’impression. Les délais restent de 15 s pour un envoi/congestion et 45 s pour la confirmation finale. La densité et le plafond de débit figurent dans le diagnostic ; une chauffe excessive ne peut pas être affirmée sans télémétrie matérielle.

`STATUS` affiche aussi le dernier diagnostic conservé en NVS : type de tentative, date et empreinte abrégée, phase, cause, octets envoyés/confirmés/prévus, lignes lues, durée, statuts Bluetooth et derniers octets reçus en hexadécimal. Un compte rendu de début est enregistré avant l’image, puis remplacé à la fin. Après une coupure brutale en cours d’envoi, il indique donc « en-cours » ; il ne prétend pas connaître le dernier octet envoyé avant la coupure. Aucun journal n’est écrit en flash par paquet, et les échecs de connexion identiques sont dédupliqués. Un échec avant l’image autorise une nouvelle connexion ; après le début d’une tentative, le blocage « incertain » empêche toujours une réimpression automatique.

Les tests C++ exécutés dans GitHub vérifient les événements d’une ancienne connexion, les erreurs et longueurs d’ACK, les réponses prématurées ou fragmentées, et la taille bornée des diagnostics. La compilation ne remplace pas la validation papier après installation du nouveau firmware.

Le SHA-256 détecte un téléchargement incomplet et deux emplacements en flash permettent de conserver l’ancien cache pendant le suivant. Ces contrôles concernent les données ; un ticket pâle ou un manque de papier doit être vérifié sur l’imprimante.

## Fichiers utiles

- `mini_journal/` : RSS, sélection IA, rendu et encodage.
- `firmware/` : programme ESP32 Classic SPP et configuration locale.
- `scripts/is_due.py`, `scripts/publish.py` : garde quotidienne et publication.
- `scripts/generate_calibration.py` : régénération du petit ticket `TEST`.
- `tests/` : vérifications des textes, du rendu et des données binaires.
- `AUDIT.md` : décisions de simplification et limites restantes.

Le protocole s’appuie sur les observations du projet [phomemo-tools](https://github.com/vivier/phomemo-tools). Les polices DejaVu et leur licence sont incluses. Le projet est distribué sous la licence présente dans `LICENSE`.
