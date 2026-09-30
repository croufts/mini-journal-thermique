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
2. Une édition RSS de secours est définitive pour la journée, comme une édition IA : les rattrapages ne la remplacent pas automatiquement. Une régénération manuelle est possible.
3. Le format 626 pixels puis adaptation à 576 est conservé pour rester compatible avec les ESP32 déjà installés. Un passage entièrement natif à 576 demanderait une migration coordonnée du générateur et du firmware et un nouveau test papier. Il n’est pas nécessaire au fonctionnement actuel.
4. GitHub Actions gratuit ne garantit pas une minute d’exécution. Les heures UTC changent d’équivalent local selon la saison.
5. Après coupure électrique, l’ESP32 doit retrouver Internet pour remettre son horloge à l’heure. Aucun composant RTC supplémentaire n’est prévu.
6. Les tests logiciels et la compilation ne prouvent pas la qualité physique. Toute nouvelle version du firmware doit être téléversée puis testée sur matériel avant de la déclarer validée.

Le transport Classic continu et la coupure du Wi-Fi ont été validés sur un cycle autonome avant cette revue. Les changements de firmware de cette revue sont distincts de ce résultat matériel.
