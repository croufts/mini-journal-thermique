# Validation du 30 septembre 2026

- 25 tests Python passent, dont les trois titres incomplets observés sur le ticket et la conservation de phrases entières et l’utilisation de la sortie relue.
- Firmware ESP32 WROOM-32D compilé et téléversé via COM4, sans effacement du cache ni de NVS.
- Petit ticket ESP32 BLE / 576 points confirmé lisible par Mathias.
- Le premier journal BLE était complet mais présentait des traces blanches périodiques avec les bandes séparées.
- La version actuelle fusionne les bandes en un seul raster et attend une seule fin d’image. Validation physique de cette correction encore nécessaire.
- Le prochain démarrage autonome, PC éteint, reste à vérifier.

Les confirmations de transport seules ne prouvent ni la présence du papier, ni la qualité du ticket.
