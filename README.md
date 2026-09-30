# Le mini-journal de Mathias

Un journal thermique matinal autonome pour une **Phomemo M02 Pro** et un **ESP32 classique**. Après installation, le PC peut rester éteint : GitHub produit le journal, l’ESP32 le conserve en flash et l’envoie lorsque l’imprimante devient disponible.

Le ticket commence par **« Bonjour Mathias. »**, puis la date et les sections **FRANCE, MONDE, TECH**. Une seule information Tech est conservée. Les titres ont plusieurs tailles, le corps est plus petit, les sections sont séparées par des traits. Aucun pictogramme, météo ou source n’est ajouté au ticket.

## Ce qui est prêt, ce qui reste à configurer

Le dépôt contient le générateur Python, les tests, les workflows GitHub, le firmware et les polices. Un exemple fictif figure dans `examples/preview.png`.

L’installation GitHub de Mathias est disponible sur [croufts/mini-journal-thermique](https://github.com/croufts/mini-journal-thermique). Les workflows de génération quotidienne et de vérification sont actifs. Le secret OpenRouter est configuré et une [édition sélectionnée et résumée par OpenRouter a été générée et publiée le 29 septembre 2026](https://github.com/croufts/mini-journal-thermique/actions/runs/36546100384).

L’adresse publique à utiliser pour cet ESP32 est :

```text
https://raw.githubusercontent.com/croufts/mini-journal-thermique/refs/heads/journal/
```

Elle est déjà renseignée dans `config.example.h`. La génération Internet, le téléchargement SHA-256 et le verrou quotidien sont opérationnels. Le firmware actuel utilise Bluetooth LE après des blocages et une qualité irrégulière en Classic. Le petit ticket BLE depuis l’ESP32 a été confirmé complet et lisible le 30 septembre. Le premier journal BLE complet présentait des traces blanches aux frontières de bandes. La version actuelle fusionne ces bandes ; sa qualité et le prochain démarrage avec le PC éteint restent à contrôler. Les identifiants Wi-Fi et l’adresse matérielle restent dans le fichier local ignoré par Git.

## Fonctionnement

```text
GitHub Actions, chaque matin
  → flux RSS récents en français, France / Monde / Tech
  → OpenRouter gratuit, puis Groq gratuit si configuré
  → validation des identifiants, des sections et du JSON
  → mise en page noir et blanc 626 × 2362 pixels
  → encodage raster M02 Pro + manifeste SHA-256
  → publication de la branche journal

ESP32 alimenté en permanence
  → Wi-Fi 2,4 GHz + heure Internet
  → lecture HTTPS du manifeste toutes les 5 minutes
  → téléchargement vérifié en flash, sans charger tout le fichier en RAM
  → détection Bluetooth LE toutes les 20 secondes, scan de 3 secondes
  → dès la connexion : envoi du journal, garde anti-doublon en mémoire permanente
```

L’ESP32 découvre la M02 Pro, établit une liaison BLE chiffrée, puis utilise FF02 pour écrire et FF03 pour recevoir ses notifications. Il envoie des blocs d’au plus 182 octets, avec quatre crédits initiaux et un bloc supplémentaire par notification `01 01`. Aucune pause fixe n’est ajoutée. Il regroupe les bandes du cache en une seule image raster de 576 points, sans pause entre les bandes, puis attend une notification `1A 0F 0C` à la fin de cette image. Les crédits et notifications signalent le traitement du protocole ; contrôler aussi le papier, le capot et la qualité réelle.

Le Wi-Fi est suspendu pendant l’impression et rétabli ensuite. La pile BLE est libérée pendant les téléchargements HTTPS. Les délais d’attente sont bornés et un envoi interrompu ne se répète pas automatiquement. Le scan précédent chaque connexion utilise l’adresse annoncée et son type ; des connexions directes sans scan avaient été refusées après réinitialisation de la pile sur cette M02 Pro.

## Gratuité

- **OpenRouter** : le modèle `openrouter/free` sélectionne un modèle gratuit disponible. Le générateur refuse les modèles OpenRouter payants. Deux tentatives maximum par fournisseur, chacune avec sélection puis relecture, puis secours éventuel ; le quota gratuit publié est de 50 requêtes/jour. [Offre officielle](https://openrouter.ai/pricing/), [routeur gratuit](https://openrouter.ai/discover).
- **Groq**, facultatif : configuré avec `openai/gpt-oss-20b`. Créer un compte au **Free tier**, sans passer au Developer plan. Les quotas exacts dépendent du compte et du modèle. Le code ne peut pas déterminer si ton compte a été converti en offre payante. [Quotas officiels](https://console.groq.com/docs/rate-limits).
- **GitHub Actions** : les runners standard sont gratuits pour les dépôts publics. Ce montage publie le journal sans token dans l’ESP32. [Conditions de facturation](https://docs.github.com/en/actions/concepts/billing-and-usage).

Ces services gratuits peuvent changer de quotas ou être temporairement indisponibles. Aucun abonnement ChatGPT n’est nécessaire au fonctionnement du projet. Avec `allow_rss_fallback: true`, si les deux API échouent, une édition clairement marquée **« Édition de secours RSS »** utilise directement les dernières descriptions RSS. Elle conserve une info Tech, mais ne prétend pas effectuer une sélection éditoriale par IA. Mettre cette option à `false` pour annuler la publication si aucune IA ne répond.

## 1. Matériel

- ESP32 **original**, typiquement `ESP-WROOM-32`, avec au moins **4 Mo de flash**. CH340 désigne l’interface USB, pas le processeur.
- Phomemo M02 Pro chargée, papier continu, capot fermé.
- Chargeur USB pour alimenter l’ESP32 quand le PC est éteint.
- Wi-Fi **2,4 GHz** avec accès Internet ; SSID et mot de passe.

La cible PlatformIO reste `esp32dev`, testée sur ESP-WROOM-32D. L’ESP8266 n’a pas de Bluetooth. Les autres variantes ESP32 nécessitent une cible et une validation matérielle propres ; elles ne sont pas testées dans ce projet. L’imprimante doit exposer le service BLE FF00, avec FF02 writable et FF03 notify. Ce service peut être absent des annonces BLE : le scan cible le nom ou la MAC, pas le UUID de service annoncé.

Aucun fil ne relie l’ESP32 à l’imprimante. Fermer l’application Phomemo sur le téléphone pendant les essais : une connexion existante peut empêcher celle de l’ESP32.

## 2. Test Python local

Installer Python **3.12** et Git. Dans PowerShell, ouvrir le dossier de ce dépôt :

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\python.exe -m pytest -q
.\.venv\Scripts\python.exe -m mini_journal --demo
```

Ouvrir `out/preview.png`. Le mode démo utilise des nouvelles fictives, aucune API ni réseau. Son manifeste est marqué `demo: true` : le publieur et le firmware le refusent.

Tester les flux sans appeler d’IA :

```powershell
.\.venv\Scripts\python.exe -m mini_journal --check-feeds
```

Tester une véritable génération, après avoir défini la clé **dans ta session locale**, sans la mettre dans un fichier du dépôt :

```powershell
$env:OPENROUTER_API_KEY = 'TA_CLE_OPENROUTER'
.\.venv\Scripts\python.exe -m mini_journal
```

Si aucune clé n’est fournie, le secours RSS est utilisé par défaut. Les flux peuvent échouer individuellement ; une édition exige des nouvelles récentes dans chacune des trois sections. Les articles sans date, plus vieux que 36 heures ou datés dans le futur sont exclus. Le prompt est limité à neuf candidats par section, avec répartition entre les flux et dédoublonnage des titres très proches.

Sorties :

| Fichier | Usage |
|---|---|
| `preview.png` | Aperçu monochrome avec résolution 300 dpi |
| `journal-AAAA-MM-JJ-empreinte.bin` | Flux raster conservé en flash ; adapté à 576 points par le firmware BLE |
| `manifest.json` | Date, nom exact du binaire, taille, dimensions, SHA-256 |
| `edition.json` | Texte réellement rendu, fournisseur utilisé et taille des caractères |

Le binaire fait environ **187 ko**. Ce n’est pas un PNG : l’ESP32 adapte les rangées binaires à la largeur retenue, sans décodage PNG et sans charger le journal entier en RAM.

## 3. Créer le dépôt GitHub

Créer sur GitHub un dépôt **public** nommé, par exemple, `mini-journal-thermique`, **sans README initial**. Publier le contenu de ce dossier à la racine du dépôt ; il ne faut pas ajouter le dossier parent du projet ChatGPT ni ses références `sources/`.

```powershell
git init -b main
git add .
git commit -m "Projet initial du mini-journal"
git remote add origin https://github.com/TON_COMPTE/mini-journal-thermique.git
git push -u origin main
```

Si Git te demande ton identité, utiliser ton nom et ton adresse GitHub dans `git config user.name` et `git config user.email`. L’authentification pour le premier push se fait avec ton compte GitHub. Ne jamais mettre de clé IA dans le code.

Dans **Settings → Secrets and variables → Actions → New repository secret**, ajouter :

| Secret | Obligatoire ? | Valeur |
|---|---|---|
| `OPENROUTER_API_KEY` | Pour le mode IA principal | Clé créée sur [OpenRouter](https://openrouter.ai/settings/keys) |
| `GROQ_API_KEY` | Non | Clé créée sur [Groq](https://console.groq.com/keys), compte gratuit |

Le token GitHub de publication est fourni automatiquement par Actions. Aucun PAT à créer, aucun token GitHub ni clé IA à placer dans l’ESP32.

Dans **Actions**, activer les workflows si demandé, puis ouvrir **Journal quotidien → Run workflow**. Le workflow **Vérifications** teste Python, génère un aperçu fictif et compile le firmware à chaque changement de code. Les mises à jour de la branche `journal` ne déclenchent pas cette compilation.

À la fin d’un premier run réussi, la branche **journal** contient le manifeste, le binaire, le texte et l’aperçu. Vérifier dans un navigateur :

```text
https://raw.githubusercontent.com/TON_COMPTE/mini-journal-thermique/refs/heads/journal/manifest.json
```

L’URL doit répondre sans connexion à GitHub. La date doit être celle du jour à Paris et `demo` doit valoir `false`.

Le dépôt public rend aussi publics le journal, son aperçu et la mention « Bonjour Mathias. ». Les clés API et le Wi-Fi ne sont pas publiés. Un dépôt privé nécessiterait un autre mécanisme de distribution, absent de cette version.

## 4. Heure du journal

La première tentative est **03:17 UTC** : **04:17 à Paris en hiver**, **05:17 en été**, pour préparer le journal avant un allumage de l’ESP32 à 06:15. Des tentatives de rattrapage ont lieu toutes les 30 minutes jusqu’à 10:47 UTC. Chaque lancement vérifie la date publiée : après une génération réussie, les suivants ne font aucun appel IA et ne republient rien. La date imprimée utilise `Europe/Paris`. Changer la ligne `cron` dans `.github/workflows/journal.yml` pour choisir une autre plage.

GitHub peut retarder ou manquer un lancement ; les rattrapages réduisent le risque sans garantir une heure précise. Si aucun ticket ne sort, vérifier d’abord que le manifeste porte la date du jour. L’ESP32 le relit toutes les cinq minutes : laisser l’imprimante allumée après une publication tardive. Un lancement manuel ou une relance le même jour ne refait pas de requête IA si une édition existe déjà. L’option **force** régénère ce jour-là ; l’ESP32 garde néanmoins son verrou d’impression par date.

Le workflow doit être présent sur la branche par défaut `main`. La branche `journal` est un instantané, remplacé à chaque publication avec une protection contre les modifications concurrentes. Ne pas l’utiliser pour conserver du code, et ne pas bloquer ses force pushes dans les règles du dépôt. Les anciens tickets ne sont pas archivés dans son historique accessible.

GitHub peut désactiver les workflows planifiés d’un dépôt public après 60 jours sans activité. Les publications régulières mettent à jour le dépôt ; si les éditions cessent, vérifier également l’état du workflow et le réactiver dans Actions. [Documentation du scheduler](https://docs.github.com/en/actions/writing-workflows/choosing-when-your-workflow-runs/events-that-trigger-workflows#schedule), [désactivation](https://docs.github.com/en/actions/managing-workflow-runs-and-deployments/managing-workflow-runs/disabling-and-enabling-a-workflow).

## 5. Configurer et téléverser l’ESP32

Le projet utilise PlatformIO, installé avec `requirements-dev.txt`. Tu peux aussi ouvrir `firmware/` dans VS Code avec l’extension PlatformIO. La plateforme est figée sur Arduino-ESP32 **2.0.17**, pour rendre la compilation reproductible avec cette API Bluetooth.

Copier `firmware/include/config.example.h` vers **`firmware/include/journal_config.h`**. Ce dernier est ignoré par Git. Modifier :

- `WIFI_SSID`, `WIFI_PASSWORD` : identifiants Wi-Fi 2,4 GHz.
- `JOURNAL_BASE_URL` : URL de la branche `journal`, **avec le `/` final**.
- `PRINTER_MAC` : adresse Bluetooth LE exacte de ta M02 Pro.
- `BT_PRINT_DENSITY` : valeur initiale de chauffe, 4 retenue après comparaison. Une valeur déjà mémorisée en NVS garde la priorité.

L’adresse peut être obtenue avec le scan décrit ci-dessous. Pour compiler et effectuer ce scan avant de connaître l’adresse, laisser la MAC d’exemple : aucune connexion utile ne se fera à cette adresse. Une MAC vide active la connexion par le nom **exact** `PRINTER_NAME`, via les annonces BLE. L’association chiffrée sans saisie de PIN est mémorisée par NimBLE.

Brancher l’ESP32 avec un câble USB de données. Le pilote CH340 peut être nécessaire si aucun port COM n’apparaît. Trouver le port :

```powershell
.\.venv\Scripts\pio.exe device list
.\.venv\Scripts\pio.exe run -d firmware
.\.venv\Scripts\pio.exe run -d firmware -t upload --upload-port COM5
```

Remplacer `COM5` par le port détecté. Si la connexion de téléversement échoue, maintenir BOOT au début de la connexion, puis relâcher lorsque le transfert commence.

**Au premier démarrage seulement**, initialiser la partition LittleFS :

```powershell
.\.venv\Scripts\pio.exe run -d firmware -t uploadfs --upload-port COM5
.\.venv\Scripts\pio.exe device monitor --port COM5 --baud 115200
```

Le firmware ne formate jamais la flash automatiquement. `uploadfs` remplace le cache ; ne pas le répéter lors d’une mise à jour normale du firmware. La garde anti-doublon est stockée dans NVS ; un effacement complet de l’ESP32 la supprime.

Dans le moniteur série, envoyer **SCAN**, avec une fin de ligne, imprimante allumée et application Phomemo fermée. Le scan affiche les adresses et noms des imprimantes M02 en BLE. Reporter l’adresse dans `journal_config.h`, fermer le moniteur et téléverser à nouveau le firmware. Si aucune M02 Pro n’apparaît, vérifier la charge, la portée et une connexion au téléphone ou au PC.

Le partitionnement prévu est **4 Mo, application 3 Mo, LittleFS environ 1 Mo**, sans OTA. Le journal utilise deux emplacements en flash ; seul un téléchargement complet avec SHA-256 correct remplace le cache actif.

## 6. Test complet, puis PC éteint

1. Générer une édition réelle via **Run workflow** et vérifier `manifest.json` dans le navigateur.
2. Laisser la M02 Pro **éteinte**. Démarrer l’ESP32 et ouvrir le moniteur série. Le journal doit être téléchargé et vérifié en flash.
3. Envoyer **STATUS** : date du jour, cache du jour, Wi-Fi OK, aucune date imprimée.
4. Allumer la M02 Pro. Observer la connexion et l’impression ; vérifier les accents, la largeur, les séparateurs, une seule info Tech et une longueur proche de 20 cm.
5. Éteindre/rallumer l’imprimante : aucune deuxième impression ce jour-là. Redémarrer également l’ESP32 avec Internet disponible : la garde doit survivre.
6. Alimenter l’ESP32 avec un chargeur USB autonome et éteindre le PC. Le lendemain, GitHub doit produire une nouvelle édition et l’allumage de l’imprimante doit déclencher celle du jour.

Le journal de la veille n’est jamais imprimé comme celui du jour. L’ESP32 attend une heure NTP valide après chaque redémarrage. Une fois l’heure connue et le journal téléchargé, il peut imprimer depuis son cache même si le Wi-Fi tombe. Après une coupure d’alimentation avec Internet indisponible, il attendra le retour de l’heure Internet.

## 7. Anti-doublons et interruptions

Avant d’envoyer le premier octet, l’ESP32 enregistre une date **pending**. Après transmission complète, contrôle des notifications de fin de chaque bande raster, il mémorise **printed**, puis retire pending. Le verrou porte sur la **date**, pas sur le hash : une régénération du jour ne provoque pas une seconde impression.

Les notifications BLE ne garantissent pas la qualité physique du ticket. Une interruption ou un redémarrage pendant l’envoi laisse un état incertain, qui bloque les nouvelles impressions, y compris les jours suivants, jusqu’à une intervention. Le verrou quotidien reste conservé après une mise à jour du firmware. Ne pas effacer NVS ou réinstaller le filesystem lors d’une mise à jour normale.

| Commande série | Effet |
|---|---|
| `STATUS` | Affiche date, cache, Wi-Fi, date transmise et envoi incertain |
| `FETCH` | Demande une vérification Internet immédiate |
| `TEST` | Imprime la comparaison courte ; ne modifie pas le cache ni la garde quotidienne |
| `DENSITY 0..4` | Calibre la chauffe et mémorise le réglage ; 0 conserve la densité native |
| `NET` | Diagnostic du signal Wi-Fi, de la mémoire, du DNS et de l’accès TCP 443 |
| `SCAN` | Recherche les annonces M02 en Bluetooth LE pendant cinq secondes |
| `RETRY` | Retire le blocage incertain ; vérifier d’abord le papier partiellement imprimé |
| `REPRINT` | Retire également le verrou de transmission et réimprime le cache du jour |

Ne pas utiliser `RETRY` avant d’avoir vérifié ce qui est sorti : un ticket partiel pourrait être répété. `REPRINT` est utile si le flux a été transmis mais que le papier manquait.

## 8. Encodage, largeur et diagnostic

Le générateur conserve un canevas noir et blanc de **626 × 2362 pixels**. Le binaire publié utilise des rangées de 79 octets, avec six bits blancs de bourrage. Le firmware BLE rééchantillonne horizontalement les 626 points utiles vers **576 points** et envoie des rangées de 72 octets ; il ne coupe pas les caractères à droite. La hauteur ne change pas, soit environ 20 cm à 300 dpi. Le cache original conserve son SHA-256. Les petits tickets natifs de 576 points sont également acceptés.

Le firmware vérifie la structure entière du job avant de commencer : en-tête ESC/POS, largeur admise, bandes de 1 à 255 lignes, hauteur totale jusqu’à 2362 et avance finale valide. Il applique l’alignement gauche et la chauffe après `ESC @`. Le protocole raster est dérivé du chemin M02 de [phomemo-tools](https://github.com/vivier/phomemo-tools) ; aucun pilote CUPS n’est installé sur l’ESP32. La liaison BLE s’appuie sur le service FF00 observé sur le matériel et le mécanisme de crédit décrit par [phomo](https://github.com/danielgormly/phomo/blob/main/Sources/phomo/BLE.swift).

`TEST` imprime un seul petit motif de 160 lignes (environ 1,35 cm avant avance), marqué **ESP32 BLE / 576 points**. Il utilise la chauffe mémorisée, affiche des textes normal et gras, une barre noire et un trait final, puis attend une notification de fin. Le test conserve le cache et le verrou quotidien. Régénérer son tableau compilé avec `python -m scripts.generate_calibration`.

La comparaison séparée des chauffes 4, 8 et 12 depuis le PC a donné trois bandes complètes et visuellement proches. La valeur **4** est retenue ; les valeurs expérimentales supérieures ne sont pas proposées par le firmware. L’autotest natif demeure la référence pour contrôler tête et papier. Des barres noires légèrement granuleuses persistent sur les photos de raster ; ne pas considérer les confirmations Bluetooth comme une validation de la qualité.

Un diagnostic PC facultatif utilise `bleak` :

```powershell
python -m pip install bleak
python -m scripts.generate_density_test
python -m scripts.ble_diagnostic --address ADRESSE_MAC_IMPRIMANTE --series out/density/series.json
```

Ce script associe la M02 Pro avec chiffrement sous Windows, envoie des jobs indépendants, attend leurs notifications de fin et garde la liaison ouverte quinze secondes entre les motifs. Il ne modifie pas NVS de l’ESP32. Le PC sert seulement au diagnostic et peut rester éteint pendant l’usage autonome.

## 9. Vérifier un problème

- Aucun ticket : vérifier la date de `manifest.json`, puis `STATUS` (date, cache, imprimé, incertain). Un journal déjà marqué imprimé ne se répète pas.
- Connexion refusée : vérifier l’allumage, la proximité et fermer l’application du téléphone ; consulter `SCAN`. Un refus avant l’envoi ne marque pas le journal comme imprimé.
- Envoi incertain : examiner le papier, éteindre/rallumer l’imprimante pour vider un raster incomplet, puis utiliser `RETRY` une fois si une répétition est souhaitée.
- Texte pâle : contrôler charge, papier et autotest natif. Comparer `TEST` avant de toucher au transport ou à la chauffe.
- Journal vide ou corrompu : la validation SHA-256 et la validation raster refusent sa transmission. L’ancien cache reste disponible après un téléchargement incomplet.

La compilation et les tests logiciels ne remplacent pas la validation d’un journal entier et du démarrage suivant, PC éteint.

### Correction des titres et des coupures du 30 septembre

Les titres terminés par un mot de liaison et les négations manifestement incomplètes sont refusés avant publication. Après la sélection, une seconde passe IA relit systématiquement les textes avec les mêmes données RSS et les mêmes identifiants. Si une tentative est refusée, la tentative suivante reçoit le motif et doit reformuler. Ces contrôles ciblés ne remplacent pas une relecture éditoriale complète. Le secours RSS conserve des titres entiers courts et des phrases entières ; si aucune sélection valide ne peut être obtenue, aucune édition n’est publiée. La mise en page ne coupe plus un résumé au milieu d’une phrase.

Le cache conserve son format historique en bandes, mais le firmware transmet une seule commande GS v 0 pour toutes les lignes. Il conserve le contrôle de débit par crédits BLE, la garde en NVS et le Wi-Fi suspendu. Aucun arrêt volontaire n’est ajouté à la frontière des anciennes bandes ; un arrêt imposé par l’imprimante reste possible et doit être contrôlé sur le papier.

### Actualisation après une impression

Le cache est actualisé lorsqu’une nouvelle empreinte est publiée, même si le journal du jour a déjà été imprimé. Le verrou quotidien reste indépendant et interdit toute réimpression automatique. La commande REPRINT impose une récupération réussie du manifeste avant l’envoi ; cette attente est mémorisée en NVS et survit au redémarrage. STATUS affiche l’empreinte du cache et si une actualisation est requise.

Mathias a confirmé que le journal envoyé en un raster continu est lisible et ne présente plus de coupures ni de traces blanches. Le premier essai continu utilisait encore l’ancien cache, car le firmware précédent ignorait les éditions corrigées une fois la date imprimée. Ce défaut de cache est corrigé dans cette version.
