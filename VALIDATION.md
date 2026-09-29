# Vérifications effectuées — 29 septembre 2026

- **20 tests Python réussis** : encodage/décodage des pixels, ordre des bits, bourrage blanc, limites des blocs 255 lignes, rejet d’un flux tronqué, maintien des trois sections et d’une seule info Tech, adaptation de la mise en page, validation des réponses IA, bascule OpenRouter → Groq simulée, secours RSS, exclusion des articles périmés/non datés, verrou quotidien et validation avant publication.
- **7 flux RSS interrogés en direct**, tous disponibles pendant le test, avec 27 candidats récents retenus dans les trois sections.
- **Génération complète avec secours RSS réel** : image monochrome 626 × 2362, manifeste et binaire de 186 686 octets. Validation et relecture de l’image encodée.
- **Exemple fictif généré et inspecté visuellement**, avec accents français, titres de différentes tailles, séparateurs et une seule info Tech.
- **Firmware compilé** avec PlatformIO espressif32 6.10.0 / Arduino-ESP32 2.0.17 / ArduinoJson 6.21.5. Occupation statique : 60 932 octets de RAM et 1 707 609 octets de programme dans la partition de 3 Mo. La mémoire utilisée à l’exécution par Wi-Fi et Bluetooth s’ajoute à cette mesure.
- **Image LittleFS construite**, prête pour l’initialisation de la partition.
- **Chaîne HTTPS de raw.githubusercontent.com observée** : elle se termine sur ISRG Root X1 ; cette racine figure dans le firmware, avec deux racines supplémentaires pour les autres chaînes GitHub.

Les appels réels à OpenRouter/Groq n’ont pas été effectués : aucune clé API n’était configurée dans la session. La logique de réponse et de secours a été vérifiée avec des réponses simulées.

Le dépôt n’a pas été publié sur un compte GitHub, et les workflows n’ont pas été exécutés sur GitHub. Le téléversement physique de l’ESP32, l’appairage SPP, les interruptions d’alimentation et l’impression sur la M02 Pro restent à vérifier selon la procédure du README.

La compilation ne garantit pas la prise en charge de Bluetooth Classic par toutes les révisions de M02 Pro. La longueur papier réelle, le bourrage à 632 positions et le débit Bluetooth doivent être confirmés lors du premier essai matériel.
