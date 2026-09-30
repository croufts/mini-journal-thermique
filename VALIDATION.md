# Vérifications effectuées — 29 septembre 2026

- **20 tests Python réussis** : encodage/décodage des pixels, ordre des bits, bourrage blanc, limites des blocs 255 lignes, rejet d’un flux tronqué, maintien des trois sections et d’une seule info Tech, adaptation de la mise en page, validation des réponses IA, bascule OpenRouter → Groq simulée, secours RSS, exclusion des articles périmés/non datés, verrou quotidien et validation avant publication.
- **7 flux RSS interrogés en direct**, tous disponibles pendant le test, avec 27 candidats récents retenus dans les trois sections.
- **Génération complète avec secours RSS réel** : image monochrome 626 × 2362, manifeste et binaire de 186 686 octets. Validation et relecture de l’image encodée.
- **Exemple fictif généré et inspecté visuellement**, avec accents français, titres de différentes tailles, séparateurs et une seule info Tech.
- **Firmware compilé** avec PlatformIO espressif32 6.10.0 / Arduino-ESP32 2.0.17 / ArduinoJson 6.21.5. Occupation statique : 60 932 octets de RAM et 1 707 609 octets de programme dans la partition de 3 Mo. La mémoire utilisée à l’exécution par Wi-Fi et Bluetooth s’ajoute à cette mesure.
- **Image LittleFS construite**, prête pour l’initialisation de la partition.
- **Chaîne HTTPS de raw.githubusercontent.com observée** : elle se termine sur ISRG Root X1 ; cette racine figure dans le firmware, avec deux racines supplémentaires pour les autres chaînes GitHub.

La logique de réponse et de secours a d’abord été vérifiée avec des réponses simulées. Groq reste facultatif et n’a pas été configuré ni appelé en direct.

Le dépôt a ensuite été publié sur [croufts/mini-journal-thermique](https://github.com/croufts/mini-journal-thermique).

- [Installation sur GitHub réussie](https://github.com/croufts/mini-journal-thermique/actions/runs/36543799242).
- [Tests Python, génération fictive et compilation ESP32/LittleFS réussis sur le runner Linux GitHub](https://github.com/croufts/mini-journal-thermique/actions/runs/36543925548).
- [Première génération réelle et publication automatique réussies](https://github.com/croufts/mini-journal-thermique/actions/runs/36543990015), avec le secours RSS en attendant la clé IA.
- Le manifeste et le binaire publics ont été téléchargés sans authentification : date du 29 septembre 2026, 186 686 octets, SHA-256 valide, relecture en 626 × 2362 pixels et exactement une info Tech.
- Le chemin explicite `refs/heads/journal` est utilisé pour éviter un cache 404 observé juste après la création de la branche sur le chemin abrégé.
- **Secret OpenRouter configuré dans GitHub**, sans clé dans le code ni dans l’ESP32. Le premier essai authentifié a reçu des réponses invalides et publié le secours RSS. Le générateur demande maintenant un schéma JSON strict avec les identifiants de chaque section, les limites de longueur et exactement une info Tech ; les réponses tronquées sont refusées.
- **[Génération IA réelle réussie après correction](https://github.com/croufts/mini-journal-thermique/actions/runs/36546100384)** : fournisseur `OpenRouter`, modèle demandé `openrouter/free`, trois France, trois Monde et une Tech. Le binaire public contient 186 686 octets et son SHA-256 est `a5a0a566f45e2ca5b9ae474f5b755e024575e25cb8ed9f1141361408ad74968c`. Le décodage raster est identique pixel par pixel à l’aperçu 626 × 2362, inspecté visuellement.
- **20 tests Python réussis après correction du format IA**, et vérifications GitHub exécutées sur le même correctif.

- **ESP32 WROOM-32D détecté et programmé sur COM4**, puce ESP32-D0WD-V3, flash 4 Mo. LittleFS initialisé une seule fois. La configuration Wi-Fi et l’adresse de l’imprimante sont dans le fichier local ignoré ; elles ne sont pas publiées.
- **Wi-Fi, heure NTP et téléchargement HTTPS vérifiés sur le matériel**. Une panne TLS observée avec le Bluetooth actif a été résolue en mettant Classic en pause pendant HTTPS, puis en le redémarrant. La validation des certificats et du SHA-256 reste active.
- **M02 Pro détectée en Classic et connexion SPP établie**. Premier envoi à 128 octets / 25 ms : ticket complet mais lignes déformées, et état incertain conservé. Second essai à 64 octets / 40 ms : les 186 686 octets mis en file ont tous été confirmés par SPP, connexion maintenue, aucune erreur et date transmise mémorisée. Mathias a confirmé que le second ticket est complet et lisible, du début jusqu’à TECH.

- **Redémarrage matériel effectué après l’impression** : cache vérifié rechargé, reconnexion Wi-Fi et heure NTP valide, date transmise du 29 septembre conservée, aucun état incertain et aucune nouvelle tentative d’impression pendant l’observation.

Les coupures pendant un envoi et le fonctionnement sur chargeur avec le PC éteint restent à vérifier selon la procédure du README.

La compilation ne garantit pas la prise en charge de Bluetooth Classic par toutes les révisions de M02 Pro. La longueur papier réelle, le bourrage à 632 positions et le débit Bluetooth doivent être confirmés lors du premier essai matériel.

## Incident du 30 septembre 2026

À 09:30 Paris, aucune exécution planifiée n’apparaissait dans Actions et le manifeste public portait encore la date du 29 septembre. Le workflow était actif sur la branche par défaut main. L’absence d’édition du jour suffit à expliquer le refus d’impression prévu par le firmware ; le fonctionnement de l’ESP32 sur chargeur n’a pas été observé par logs. La cause exacte du lancement manquant côté GitHub n’est pas établie.

La planification démarre désormais à 03:17 UTC, avec rattrapage toutes les trente minutes jusqu’à 10:47 UTC. Le garde de date existant conserve une seule génération par jour après publication réussie. Une génération manuelle du 30 septembre a été déclenchée pour rétablir l’édition du jour. La fiabilité des prochaines exécutions planifiées reste à observer.

## Qualité du ticket du 30 septembre

Le ticket retrouvé par Mathias portait la date du 30 septembre mais était pâle et déformé dans sa partie basse. Le fichier publié, son SHA-256 et son décodage ont été vérifiés : pixels identiques à l’aperçu, aucun octet 0A dans le raster. L’ESP32 avait conservé la date du 30 en état incertain. Cela ne permet pas d’affirmer une cause unique de la pâleur ou des déformations.

L’autotest de la M02 Pro après branchement au chargeur a été confirmé net par Mathias. Deux essais avec densité explicite 4 et attente de confirmation après chaque bloc se sont interrompus avant 1 ko ; la cause précise n’est pas démontrée. Ces réglages ne sont pas retenus par défaut. Mathias a demandé de retrouver la vitesse initiale : 128 octets / 25 ms. Cette version intermédiaire gardait la densité native et attendait les confirmations cumulées en fin d’envoi, avec une limite de quinze secondes. Elle journalise aussi la progression et réenregistre le callback après reprise Bluetooth. La commande DENSITY 0..4 permet un essai contrôlé sans recompilation et conserve la valeur en NVS. La validation visuelle de cette étape a révélé des pertes de données, corrigées dans les étapes suivantes.

Avec la file asynchrone à 128/25, le journal complet avait été mis en file mais seulement 127826/186686 octets confirmés ; Mathias confirmait un ticket pâle ou déformé. Le code de BluetoothSerial 2.0.17 utilise un délai interne d’une seconde et peut abandonner un paquet après acceptation par write. Le firmware a été modifié pour appeler directement esp_spp_write, attendre chaque ESP_SPP_WRITE_EVT et respecter ESP_SPP_CONG_EVT, selon l’API officielle Espressif. La pause de base reste à 25 ms. Compilation et téléversement réussis ; résultats du contrôle physique ci-dessous.

Premier essai avec esp_spp_write direct à 128 octets / 25 ms et densité native : 186686/186686 octets confirmés, connexion OK, garde printed=2026-09-30 et pending vide. Mathias a confirmé un ticket entièrement lisible mais encore pâle. L’autotest montré ensuite indiquait 300 dpi, batterie 54 %, densité native 2 et version 2.0.6.B, avec un noir net. Un essai du même journal avec DENSITY 4 a ensuite été réalisé, avec réglage mémorisé en NVS. Aucun identifiant matériel ni photo personnelle n’est publié dans le dépôt.

Résultat final DENSITY 4 : STATUS indique printed=2026-09-30, pending vide et Wi-Fi OK après l’essai. La fin de la trace d’envoi n’a pas été capturée pendant le changement de lecteur série, mais la garde printed est enregistrée uniquement après contrôle de tous les octets. Mathias a confirmé et montré un journal complet et lisible jusqu’à TECH, encore un peu lent et légèrement pâle par endroits. Le niveau 4 est conservé en NVS sur son ESP32. Le corps du ticket est identique à l’édition publiée ; la photo reste locale. Les réglages de cadence sont 128 octets / 25 ms, avec attente des confirmations et de la décongestion : le temps réel dépasse donc le minimum théorique.
