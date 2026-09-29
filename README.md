# Le mini-journal de Mathias

Un journal thermique matinal autonome pour une **Phomemo M02 Pro** et un **ESP32 classique**. Après installation, le PC peut rester éteint : GitHub produit le journal, l’ESP32 le conserve en flash et l’envoie lorsque l’imprimante devient disponible.

Le ticket commence par **« Bonjour Mathias. »**, puis la date et les sections **FRANCE, MONDE, TECH**. Une seule information Tech est conservée. Les titres ont plusieurs tailles, le corps est plus petit, les sections sont séparées par des traits. Aucun pictogramme, météo ou source n’est ajouté au ticket.

## Ce qui est prêt, ce qui reste à configurer

Le dépôt contient le générateur Python, les tests, les workflows GitHub, le firmware et les polices. Un exemple fictif figure dans `examples/preview.png`.

L’installation GitHub de Mathias est disponible sur [croufts/mini-journal-thermique](https://github.com/croufts/mini-journal-thermique). Les workflows de génération quotidienne et de vérification sont actifs. Une première édition de secours RSS a été générée et publiée le 29 septembre 2026.

L’adresse publique à utiliser pour cet ESP32 est :

```text
https://raw.githubusercontent.com/croufts/mini-journal-thermique/refs/heads/journal/
```

Elle est déjà renseignée dans `config.example.h`. Pour activer la sélection IA, renseigner le secret OpenRouter décrit ci-dessous. Il reste également à configurer le Wi-Fi et l’adresse Bluetooth de l’imprimante, puis téléverser l’ESP32. L’impression physique et la compatibilité SPP de **ton exemplaire** doivent être testées sur place.

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
  → tentative Bluetooth Classic toutes les 20 secondes
  → dès la connexion : envoi du journal, garde anti-doublon en mémoire permanente
```

L’allumage de l’imprimante entraîne une impression après sa détection, habituellement dans la minute, en tenant compte de la durée de connexion. L’envoi prend environ 40 secondes avec les réglages prudents par défaut.

## Gratuité

- **OpenRouter** : le modèle `openrouter/free` sélectionne un modèle gratuit disponible. Le générateur refuse les modèles OpenRouter payants. Deux tentatives maximum par édition, puis secours éventuel ; le quota gratuit publié est de 50 requêtes/jour. [Offre officielle](https://openrouter.ai/pricing/), [routeur gratuit](https://openrouter.ai/discover).
- **Groq**, facultatif : configuré avec `openai/gpt-oss-20b`. Créer un compte au **Free tier**, sans passer au Developer plan. Les quotas exacts dépendent du compte et du modèle. Le code ne peut pas déterminer si ton compte a été converti en offre payante. [Quotas officiels](https://console.groq.com/docs/rate-limits).
- **GitHub Actions** : les runners standard sont gratuits pour les dépôts publics. Ce montage publie le journal sans token dans l’ESP32. [Conditions de facturation](https://docs.github.com/en/actions/concepts/billing-and-usage).

Ces services gratuits peuvent changer de quotas ou être temporairement indisponibles. Aucun abonnement ChatGPT n’est nécessaire au fonctionnement du projet. Avec `allow_rss_fallback: true`, si les deux API échouent, une édition clairement marquée **« Édition de secours RSS »** utilise directement les dernières descriptions RSS. Elle conserve une info Tech, mais ne prétend pas effectuer une sélection éditoriale par IA. Mettre cette option à `false` pour annuler la publication si aucune IA ne répond.

## 1. Matériel

- ESP32 **original**, typiquement `ESP-WROOM-32`, avec au moins **4 Mo de flash**. CH340 désigne l’interface USB, pas le processeur.
- Phomemo M02 Pro chargée, papier continu, capot fermé.
- Chargeur USB pour alimenter l’ESP32 quand le PC est éteint.
- Wi-Fi **2,4 GHz** avec accès Internet ; SSID et mot de passe.

Les ESP8266, ESP32-S2/S3/C3/C6 ne conviennent pas à ce firmware Bluetooth Classic. Certaines révisions d’imprimantes utilisent uniquement le BLE : si le scan Classic ne trouve pas la M02 Pro, vérifier la révision avant de chercher à modifier l’encodage. Ce firmware implémente le transport **SPP Classic**, pas le BLE.

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
| `journal-AAAA-MM-JJ-empreinte.bin` | Flux de commandes envoyé directement à l’imprimante |
| `manifest.json` | Date, nom exact du binaire, taille, dimensions, SHA-256 |
| `edition.json` | Texte réellement rendu, fournisseur utilisé et taille des caractères |

Le binaire fait environ **187 ko**. Ce n’est pas un PNG : il ne nécessite aucune conversion sur l’ESP32.

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

La planification par défaut est **04:17 UTC** : **05:17 à Paris en hiver**, **06:17 en été**. La date imprimée utilise `Europe/Paris`. Changer la ligne `cron` dans `.github/workflows/journal.yml` pour choisir une autre heure.

GitHub peut retarder ou manquer un lancement ; prévoir de la marge avant ton réveil. Un lancement manuel ou une relance le même jour ne refait pas de requête IA si une édition existe déjà. L’option **force** régénère ce jour-là ; l’ESP32 garde néanmoins son verrou d’impression par date.

Le workflow doit être présent sur la branche par défaut `main`. La branche `journal` est un instantané, remplacé à chaque publication avec une protection contre les modifications concurrentes. Ne pas l’utiliser pour conserver du code, et ne pas bloquer ses force pushes dans les règles du dépôt. Les anciens tickets ne sont pas archivés dans son historique accessible.

GitHub peut désactiver les workflows planifiés d’un dépôt public après 60 jours sans activité. Les publications régulières mettent à jour le dépôt ; si les éditions cessent, vérifier également l’état du workflow et le réactiver dans Actions. [Documentation du scheduler](https://docs.github.com/en/actions/writing-workflows/choosing-when-your-workflow-runs/events-that-trigger-workflows#schedule), [désactivation](https://docs.github.com/en/actions/managing-workflow-runs-and-deployments/managing-workflow-runs/disabling-and-enabling-a-workflow).

## 5. Configurer et téléverser l’ESP32

Le projet utilise PlatformIO, installé avec `requirements-dev.txt`. Tu peux aussi ouvrir `firmware/` dans VS Code avec l’extension PlatformIO. La plateforme est figée sur Arduino-ESP32 **2.0.17**, pour rendre la compilation reproductible avec cette API Bluetooth.

Copier `firmware/include/config.example.h` vers **`firmware/include/journal_config.h`**. Ce dernier est ignoré par Git. Modifier :

- `WIFI_SSID`, `WIFI_PASSWORD` : identifiants Wi-Fi 2,4 GHz.
- `JOURNAL_BASE_URL` : URL de la branche `journal`, **avec le `/` final**.
- `PRINTER_MAC` : adresse Bluetooth Classic exacte de ta M02 Pro.
- Si l’appairage exige un PIN : `BT_REQUIRE_PIN true`, puis le PIN réel dans `BT_PIN`.

L’adresse peut être obtenue avec le scan décrit ci-dessous. Pour compiler et effectuer ce scan avant de connaître l’adresse, laisser la MAC d’exemple : aucune connexion utile ne se fera à cette adresse. Une MAC vide active la connexion par le nom **exact** `PRINTER_NAME`, plus lente et utilisant l’authentification de la bibliothèque.

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

Dans le moniteur série, envoyer **SCAN**, avec une fin de ligne, imprimante allumée et application Phomemo fermée. Le scan affiche les adresses et noms des appareils Classic. Reporter l’adresse dans `journal_config.h`, fermer le moniteur et téléverser à nouveau le firmware. Si aucune M02 Pro n’apparaît, vérifier la charge, la portée, l’occupation par le téléphone et la prise en charge de Classic SPP.

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

Avant d’envoyer le premier octet, l’ESP32 enregistre une date **pending**. Après transmission complète, contrôle des événements SPP et vidage de la file d’envoi, il mémorise **printed**, puis retire pending. Le verrou porte sur la **date**, pas sur le hash : une régénération du jour ne provoque pas une seconde impression.

Les confirmations SPP concernent le transport des octets ; elles **ne prouvent pas** que le papier a été imprimé ou que le rouleau était présent. Une interruption ou un redémarrage pendant l’envoi laisse un état incertain, qui bloque les nouvelles impressions, y compris les jours suivants, jusqu’à ton intervention. Cela privilégie l’absence de doublon automatique. L’indication « transmis » signifie que le flux a été envoyé, pas qu’un capteur a confirmé le ticket physique.

| Commande série | Effet |
|---|---|
| `STATUS` | Affiche date, cache, Wi-Fi, date transmise et envoi incertain |
| `FETCH` | Demande une vérification Internet immédiate |
| `SCAN` | Recherche les appareils Bluetooth Classic pendant 10 secondes |
| `RETRY` | Retire le blocage incertain ; vérifier d’abord le papier partiellement imprimé |
| `REPRINT` | Retire également le verrou de transmission et réimprime le cache du jour |

Ne pas utiliser `RETRY` avant d’avoir vérifié ce qui est sorti : un ticket partiel pourrait être répété. `REPRINT` est utile si le flux a été transmis mais que le papier manquait.

## 8. Encodage et réglages papier

La largeur demandée est **626 pixels**. Le protocole raster exige des octets entiers : chaque ligne transporte **79 octets**, soit 632 positions, avec **six bits de bourrage blancs** à droite. Le texte garde des marges latérales de 26 pixels. La compatibilité de cette largeur doit être confirmée lors de ton premier test matériel.

Le flux par défaut utilise `ESC @`, justification centrée, puis `GS v 0` en blocs de **255 lignes maximum**, dimensions little-endian, points noirs à 1, bit de poids fort en premier, et `ESC d` pour l’avance finale. C’est une implémentation indépendante du protocole documenté et du chemin CUPS M02/M02 Pro de [phomemo-tools](https://github.com/vivier/phomemo-tools), référence vérifiée : `d0522f058df7915674640b71aa6256d96bde4fd6`. Aucun pilote CUPS n’est requis sur l’ESP32.

Le choix des 626 pixels est également documenté par [Phomymo](https://github.com/transcriptionstream/phomymo). Les firmwares et transports des différentes révisions ne sont pas tous identiques. Deux options dans `config.json` permettent de tester des variantes :

- `prefix: true` ajoute le préfixe `10 FF FE 01` utilisé sur un autre chemin M02. Par défaut, il est absent pour suivre le filtre CUPS Classic.
- `legacy_lf_workaround: true` remplace dans les pixels `0A` par `14`, contournement de l’ancien outil M02. **Il altère certains pixels** ; il est désactivé par défaut et ne doit être testé qu’en cas de lignes cassées.

Un canevas de 2362 lignes vaut environ **200 mm à 300 dpi**, auxquels s’ajoutent l’avance finale et les tolérances de l’imprimante. Pour réduire la longueur physique, baisser `printer.height` dans `config.json` ou `feed_lines` (deux par défaut). La mise en page refuse toute coupe de texte : elle réduit le corps jusqu’à 27 pixels, retire les brèves de moindre importance puis raccourcit les résumés si nécessaire. Une section France, une Monde et la Tech restent présentes.

Si l’imprimante imprime des caractères incohérents, vérifier d’abord le modèle, le transport et le canal SPP. Si le ticket présente des coupures, augmenter `BT_CHUNK_DELAY_MS` à 40 ou 60 et éventuellement diminuer `BT_CHUNK_BYTES` à 64, puis retester. En cas d’envoi incertain, utiliser la procédure `RETRY` ci-dessus.

## 9. Diagnostic et entretien

Les logs GitHub indiquent les flux disponibles, le nombre de candidats, le fournisseur retenu ou le secours RSS, et les dimensions finales. Les erreurs API n’affichent ni les clés ni les réponses complètes. L’aperçu reste disponible dans les artifacts pendant trois jours. Le texte publié ne contient pas les URLs des sources RSS.

| Symptôme | Vérification |
|---|---|
| Pas de journal du jour | Actions, état du workflow, erreur RSS/IA, permissions `contents: write`, protection de la branche `journal` |
| HTTP 404 sur ESP32 | URL, dépôt public, branche `journal`, premier run terminé ; `FETCH` pour réessayer |
| Wi-Fi absent | Réseau 2,4 GHz, mot de passe, alimentation USB |
| Cache présent mais aucune impression | `STATUS`, heure Internet, date du cache, garde pending/printed, connexion téléphone |
| Erreur flash | Initialisation LittleFS au premier usage, carte réellement dotée de 4 Mo |
| Erreur HTTPS | Heure NTP, accès réseau, certificats racines à jour |
| OpenRouter indisponible | Quota, clé, politiques de modèles gratuits ; secours Groq ou RSS dans les logs |

HTTPS vérifie le certificat et le nom du serveur. `firmware/include/tls_roots.h` inclut ISRG Root X1, DigiCert Global Root G2 et Sectigo R46 pour les chaînes GitHub. Si les certificats changent, mettre à jour `certifi`, exécuter `python -m scripts.update_tls_roots`, vérifier les racines nécessaires et téléverser le firmware ; ne pas désactiver la validation TLS.

L’ESP32 n’expose pas de serveur Web ni de secrets IA. Son mot de passe Wi-Fi demeure dans son firmware et dans ton fichier local ignoré. La publication est publique et se limite aux articles, à l’aperçu et au fichier d’impression. L’IA reçoit des titres et descriptions RSS ; elle ne reçoit pas ton Wi-Fi.

## Structure du dépôt

```text
.github/workflows/    Génération quotidienne et vérifications
mini_journal/         Flux, IA, mise en page, protocole, commande de génération
firmware/             Projet PlatformIO ESP32 Classic
scripts/              Publication, contrôle de date, certificats HTTPS
tests/                Tests du protocole, mise en page, RSS, IA et publication
assets/fonts/         Polices DejaVu avec leur licence
examples/             Exemple fictif et aperçu
config.json           Flux et paramètres éditoriaux/imprimante
```

Le code original du projet est sous licence MIT. Les polices sont sous leur licence propre dans `assets/fonts/LICENSE`. Les projets de reverse engineering sont cités comme références ; leur code n’est pas incorporé.
