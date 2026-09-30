# Validation du 30 septembre 2026

- 25 tests Python passent, dont les trois titres incomplets observés sur le ticket et la conservation de phrases entières et l’utilisation de la sortie relue.
- Firmware ESP32 WROOM-32D compilé et téléversé via COM4, sans effacement du cache ni de NVS.
- Petit ticket ESP32 BLE / 576 points confirmé lisible par Mathias.
- Le premier journal BLE était complet mais présentait des traces blanches périodiques avec les bandes séparées.
- La version actuelle fusionne les bandes en un seul raster et attend une seule fin d’image. Mathias a confirmé l’absence de coupures et de traces blanches, et la lisibilité de tout le ticket.
- Le prochain démarrage autonome, PC éteint, reste à vérifier.

Les confirmations de transport seules ne prouvent ni la présence du papier, ni la qualité du ticket.

Le premier tirage continu conservait le texte de l’ancien cache. Correction : téléchargement des nouvelles empreintes même après impression, actualisation obligatoire et persistante avant REPRINT, empreinte affichée dans STATUS.


## Contrôle global du 30 septembre, avant l’essai de 17h45

- Dernier tirage continu confirmé parfait par Mathias ; hauteur désormais adaptée au contenu.
- 29 tests Python passent. Firmware compilé et installé sur ESP32 WROOM-32D via COM6, sans effacement NVS/LittleFS.
- Les sept flux RSS répondent ; 27 candidats récents répartis dans les trois sections.
- DNS, TCP 443 et HTTPS vers GitHub fonctionnent depuis ESP32 ; signal Wi-Fi mesuré à -24 dBm. Racines TLS valables jusqu’en 2035, 2038 et 2046.
- Génération complète et publication vérifiées via Journal quotidien #11 et #12. OpenRouter a encore renvoyé des réponses inutilisables ; le secours RSS réparé publie une édition valide avec exactement une rubrique Tech. La qualité de sélection de ce secours reste moins bonne que celle de l’IA.
- Nouveau mode de reprise OpenRouter en JSON simple, conservant la relecture et la validation. Le secours garde les titres RSS longs entiers dans le corps.
- Manifeste, SHA-256, décodage raster et aperçu vérifiés. Cache ESP32 confirmé avec empreinte 38892f64b3a9703e.
- Redémarrage matériel testé : cache, anti-doublon et programmation ponctuelle persistent. Synchronisation NTP acquise après le démarrage.
- Essai unique programmé dans ESP32 au timestamp 1790783100 (17h45 Europe/Paris). À l’échéance, actualisation puis réimpression ; désarmement avant envoi ; échéance trop ancienne annulée. Aucun tirage anticipé.
- Horaire quotidien conservé : première tentative 03h17 UTC, rattrapages toutes les 30 minutes jusqu’à 10h47 UTC. GitHub peut retarder l’exécution. ESP32 consulte toutes les cinq minutes et cherche l’imprimante toutes les vingt secondes.
- Les credentials locaux restent ignorés par Git. Confirmation physique de l’essai programmé et prochain matin autonome encore attendues.

- Le secours RSS exclut aussi les critiques Netflix/Arte, le casting et GTA présents dans les flux mixtes, pour conserver une actualité Tech.
