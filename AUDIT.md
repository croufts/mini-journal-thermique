# Revue du projet — 30 septembre 2026

## Corrections et simplifications

| Constat | Décision |
|---|---|
| Nom personnel codé dans les titres, le Bluetooth, les tests et la documentation | Nom générique ; salutation configurable par `JOURNAL_GREETING` ou `config.json` |
| README et carnet de validation mêlant BLE, SPP et essais périmés | Un README décrivant uniquement le fonctionnement actuel |
| Ancien transport BLE et outils de chauffe expérimentaux inutilisés | Retirés ; un transport Classic SPP et un petit ticket `TEST` |
| Réglages CHUNK, PACE, canal et délais sans effet | Retirés de la configuration et des commandes |
| Workflow d’initialisation dépendant d’une archive inexistante | Retiré ; installation par copie ou fork |
| Diagnostic API dupliquant la production et conservant le paramètre erroné | Retiré ; mode sans publication et option IA obligatoire dans le workflow quotidien |
| Rejet d’un brouillon trop long avant sa relecture ; limites strictes susceptibles de couper des mots lors du décodage du modèle | Brouillon transmis entier à la relecture ; longueurs vérifiées sur le résultat final |
| Raisonnement interdit pour certains modèles qui l’imposent | Contrainte retirée ; même modèle gratuit pour la relecture, budget de réponse augmenté (correction précédente) |
| Trois vérifications SHA du même téléchargement, puis nouvelle lecture toutes les vingt secondes | Vérification à réception et chargement du cache ; suppression des lectures répétées avant chaque connexion |
| Envoi incertain bloquant toutes les journées suivantes | Blocage limité à sa journée ; une nouvelle date autorise la nouvelle édition |
| Manifeste périmé considéré comme actualisation réussie | Il reste en attente jusqu’à disponibilité de l’édition du jour |
| Ancienne substitution de l’octet 0x0A dégradant les pixels | Option supprimée ; données binaires conservées intactes |
| Décodeur acceptant une avance papier invalide et erreur non maîtrisée sur un en-tête tronqué | Rejet explicite de ces fichiers |
| Dépendances réinstallées lors de chaque rattrapage inutile | Vérification de la date avant installation des dépendances |
| Polices rechargées pour chaque bloc et tentative de mise en page | Petit cache des polices par taille |
| Actions GitHub sous Node 20 déprécié, image Ubuntu changeante | Actions actualisées et runner Ubuntu 24.04 explicite |

| Erreurs API contenues dans une réponse HTTP 200 masquées par un KeyError | Lecture explicite de l’enveloppe et message borné sans clé ; contenu vide distingué du JSON invalide |

## Mécanismes conservés volontairement

- Sélection puis relecture IA : la première sortie a effectivement présenté des titres coupés. Il ne s’agit pas de deux validations identiques.
- Contrôle des identifiants RSS, des rubriques et des longueurs : empêche une réponse inutilisable d’atteindre le rendu.
- Date imprimée et date d’envoi incertain en NVS : évitent les doubles tickets après redémarrage.
- Cache alterné et empreinte : un téléchargement raté ne remplace pas le fichier valide.
- Contrôle de congestion SPP et confirmation de fin d’image : nécessaires à l’impression continue déjà validée sur papier.
- Rattrapages GitHub : utiles en cas de retard du planificateur ou des flux.

## Limites restantes

1. La qualité éditoriale dépend du modèle gratuit et des extraits RSS. Le code ne peut pas certifier la grammaire ou la véracité de chaque reformulation ; les contrôles linguistiques restent ciblés.
2. Une indisponibilité prolongée d’OpenRouter peut retarder ou empêcher l’édition du jour. Aucun secours RSS brut n’est publié. La garde de date ne s’applique qu’après publication réussie.
3. Le format 626 pixels puis adaptation à 576 est conservé pour rester compatible avec les ESP32 déjà installés. Un passage entièrement natif à 576 demanderait une migration coordonnée du générateur et du firmware et un nouveau test papier. Il n’est pas nécessaire au fonctionnement actuel.
4. GitHub Actions gratuit ne garantit pas une minute d’exécution. Le cycle est désormais réglé à 05:00 Europe/Paris avec adaptation saisonnière ; un déclenchement direct par l’ESP32 évite d’attendre le cron, mais nécessite un jeton Actions valide et une connexion Internet.
5. Après coupure électrique, l’ESP32 doit retrouver Internet pour remettre son horloge à l’heure. Aucun composant RTC supplémentaire n’est prévu.
6. Les tests logiciels et la compilation ne prouvent pas la qualité physique. Toute nouvelle version du firmware doit être téléversée puis testée sur matériel avant de la déclarer validée.

Le transport Classic continu et la coupure du Wi-Fi ont été validés sur un cycle autonome avant cette revue. Les changements de firmware de cette revue sont distincts de ce résultat matériel.


## Incident du 1er octobre et corrections

Le premier workflow de la journée a été lancé à 12:07 Paris, et publié à 12:15. Aucun lancement matinal n’est enregistré. L’ESP32 a bien reçu cette édition ; la connexion à la M02 Pro a ensuite abouti après allumage et arrêt du Bluetooth du téléphone. Les logs montrent une date imprimée au 1er octobre, sans envoi incertain.

- Démarrage quotidien à 05:00 Europe/Paris dans le workflow et le firmware.
- Déclenchement direct du workflow par l’ESP32 si le journal manque, avec un jeton limité à Actions sur ce dépôt. La demande est espacée de dix minutes tant que le fichier reste absent.
- Plus de réinitialisation périodique du Bluetooth pour consulter un manifeste déjà reçu.
- Trois connexions refusées relancent uniquement la pile Bluetooth, sans toucher au cache ou à la date imprimée.
- Après un redémarrage d’une journée déjà imprimée, le Wi-Fi se coupe immédiatement dès que l’heure est valide ; il se réactive au prochain cycle de 05:00.

### Validation du 4 octobre 2026

Le transfert direct par `esp_spp_write` restait sans confirmation après douze paquets, même avec une pause de 10 ms ou une taille de paquet réduite. Le transport utilise désormais `BluetoothSerial.write`, avec un seul paquet de 330 octets en attente et un contrôle de sa confirmation. Le journal complet du 4 octobre a été transmis : 2 319 lignes, 166 988 octets, 38,17 secondes. La fin de raster a été reçue, la journée mémorisée comme imprimée et le Wi-Fi coupé. La sortie physique a également été confirmée complète et lisible jusqu’à la section TECH.


## Génération OpenRouter — 4 octobre 2026

La génération est séparée en sélection d’identifiants, rédaction, relecture et corrections ciblées. Les articles conformes sont conservés lors des corrections ; un brouillon valide peut survivre à une panne de relecture, avec diagnostic explicite. Les limites imprimées restent à 65/240 caractères et 220 mots, tandis que le budget API augmente à 16 384 puis 32 768 tokens. Le nombre d’appels et le temps total sont bornés.

Groq, sa configuration et le secours RSS brut sont retirés. Une réponse invalide ne publie plus une édition médiocre qui empêcherait les rattrapages du jour. Les diagnostics conservent le modèle, l’étape, les longueurs fautives et les tokens déclarés, sans clé ni réponse brute. Les échecs d’authentification ou de quota ne déclenchent pas une boucle immédiate.

Ces changements concernent la génération du texte. L’essai matériel de 12h30 s’est arrêté après la salutation ; l’ESP a mémorisé un envoi incertain. La réussite matérielle consignée plus haut concerne le test précédent et ne valide pas ce nouveau cycle après coupure d’alimentation. Les tests d’impression sont arrêtés à la demande de l’utilisateur.
