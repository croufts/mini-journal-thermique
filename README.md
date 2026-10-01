# Mini-journal thermique

Un journal matinal autonome pour **Phomemo M02 Pro et ESP32 original**. GitHub Actions récupère les actualités, une API IA gratuite sélectionne et résume les nouvelles, puis l’ESP32 imprime le ticket lorsque l’imprimante est allumée. Le PC peut rester éteint.

Le ticket contient une salutation personnalisable, la date, **FRANCE**, **MONDE** et exactement une information **TECH**. Noir et blanc, titres hiérarchisés, séparateurs, sans météo, icônes ni sources affichées. La longueur suit le texte, avec un maximum d’environ 20 cm.

## Fonctionnement

```text
GitHub Actions → RSS → OpenRouter (ou Groq) → sélection et relecture
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
2. Dans **Settings → Secrets and variables → Actions**, ajouter le secret `OPENROUTER_API_KEY`. `GROQ_API_KEY` est facultatif, pour un second fournisseur.
3. Facultatif : dans l’onglet **Variables**, définir `JOURNAL_GREETING`, par exemple `Bonjour Camille.`. Sans variable, la salutation vient de `config.json` et vaut `Bonjour.`.
4. Lancer **Actions → Journal quotidien → Run workflow**.
5. Vérifier l’aperçu dans les artifacts et le manifeste public :

```text
https://raw.githubusercontent.com/TON_COMPTE/mini-journal-thermique/refs/heads/journal/manifest.json
```

Le workflow indique le fournisseur utilisé, y compris `RSS (secours)` si les API échouent. La branche `journal` contient uniquement la dernière édition. Les clés API restent dans les secrets GitHub ; l’ESP32 n’en a pas besoin.

Le mode manuel permet de forcer une nouvelle édition, de **décocher la publication pour tester sans imprimer**, et d’exiger une IA pour vérifier le service sans secours RSS. Le test utilise le même générateur que le journal quotidien.

## Horaires et configuration

Le workflow est programmé à **05:00 Europe/Paris**, avec des rattrapages à :17 et :47 jusqu’à 12:47. Le champ `timezone` suit automatiquement l’heure d’été et d’hiver. Une édition déjà publiée pour la date du jour arrête les tentatives suivantes avant installation des dépendances et appel IA. Les horaires se modifient dans `.github/workflows/journal.yml` et `DAILY_START_HOUR` côté ESP32.

GitHub peut retarder ou manquer un lancement planifié. Pour éviter de dépendre de ce cron, l’ESP32 peut envoyer directement un `workflow_dispatch` à 05:00 si l’édition du jour manque. Si elle reste indisponible, il redemande la génération au maximum toutes les dix minutes ; cette limite survit au redémarrage. Le workflow conserve sa garde de date et ne régénère pas une édition déjà publiée. Ce déclenchement direct ne garantit pas la disponibilité du réseau, du runner ou de l’imprimante.

Pour l’activer, créer un **fine-grained personal access token** GitHub limité au seul dépôt du journal, avec **Actions : Read and write** (Metadata en lecture est automatique). Copier sa valeur dans `GITHUB_ACTIONS_TOKEN` dans le fichier local ignoré `firmware/include/journal_config.h`, puis téléverser le firmware. Aucun droit Contents en écriture n’est requis pour ce jeton. Vérifier sa date d’expiration et le remplacer avant échéance. Ne jamais le committer ni publier les binaires compilés avec cette configuration. Sans jeton, le cron reste le seul déclencheur.

Les workflows planifiés des dépôts publics peuvent être désactivés après une période d’inactivité : vérifier Actions si la génération cesse.

`config.json` contient les flux, le fuseau, l’âge maximal des nouvelles, les modèles et la mise en page. Par défaut : sept flux, nouvelles des dernières 36 heures, neuf candidats maximum par rubrique. Les sources sans date ou indisponibles sont ignorées ; chaque rubrique doit conserver au moins un candidat.

OpenRouter utilise `openrouter/free` ou un modèle se terminant par `:free`. La sélection est relue avec le même modèle gratuit lorsque son identifiant est retourné. Le code respecte les modèles qui imposent le raisonnement. Groq est facultatif et doit être utilisé avec un compte gratuit. Les quotas restent ceux des fournisseurs.

Si `allow_rss_fallback` vaut `true`, une indisponibilité IA produit une **édition de secours RSS**, signalée sur le ticket. Ce secours utilise des titres et phrases RSS entiers ; il ne remplace pas la sélection éditoriale de l’IA. Mettre l’option à `false` pour ne rien publier en cas d’échec IA.

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

`out/preview.png` montre le résultat. La démo est fictive, hors ligne et ne peut pas être publiée par le script. Pour tester l’IA localement, définir `OPENROUTER_API_KEY` dans l’environnement puis lancer `python -m mini_journal --require-ai`.

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

Le fichier cache conserve son format historique en bandes. Le firmware adapte horizontalement les 626 pixels à **576 points** et transmet **une seule image raster continue**. Les paquets SPP de 512 octets sont confirmés et respectent la congestion, sans pause fixe. La fin d’image est confirmée avant de mémoriser l’impression.

Le SHA-256 détecte un téléchargement incomplet et deux emplacements en flash permettent de conserver l’ancien cache pendant le suivant. Ces contrôles concernent les données ; un ticket pâle ou un manque de papier doit être vérifié sur l’imprimante.

## Fichiers utiles

- `mini_journal/` : RSS, sélection IA, rendu et encodage.
- `firmware/` : programme ESP32 Classic SPP et configuration locale.
- `scripts/is_due.py`, `scripts/publish.py` : garde quotidienne et publication.
- `scripts/generate_calibration.py` : régénération du petit ticket `TEST`.
- `tests/` : vérifications des textes, du rendu et des données binaires.
- `AUDIT.md` : décisions de simplification et limites restantes.

Le protocole s’appuie sur les observations du projet [phomemo-tools](https://github.com/vivier/phomemo-tools). Les polices DejaVu et leur licence sont incluses. Le projet est distribué sous la licence présente dans `LICENSE`.
