# Planning des applications à corriger / créer

## A Retravailler (UX/UI, Autres bugs connus)

- **Velib** : bon bah pas qu'une liste quoi...
- **HomeApp** : Possibilité de réorganiser les applications, de changer les couleurs d'icone (et l'icone pourquoi pas)
- **Calculatrice** : Récupérer le firmware de numworks ?
- **Météo** : Corriger le fait que le background mis en fonction du temps ne soit pas supprimé sur d'autres applis...
- **SMS** : Ajouter une section "contacts" pour éviter de devoir retaper les numéros à chaque fois, possibilité d'importer depuis un fichier JSON (à faire à la main pour l'instant)
- **Horloge** : Faire une vrai appli pas que le timer
- **Télégram** : Revoir l'interface pour match SMSApp, et synchroniser correctement les contacts entre les deux 
- **Contacts** : Bah vraiment synchroniser les contacts (entre SMS, Télégram, bientôt Mail)
- **(Web)Radio** : Ajouter une section favoris, possibilité d'ajouter une station radio à la main, et corriger le fait que ca marche juste pas...


## A créer de toutes piècecs

- **Visionneur RSS** : pour lire les news, se rensigner, possibilité d'en mettre des custom dans une app à part (possibilité simple de créer des apps)
- **App Store** : Trouver un moyen simple d'ajouter des applis, avec ou sans recompilation firmware à chaque coup. 
_Ce qui a été vu pour l'appstore pour l'instant : précompiler un firmware launcher, et faire des .bin qu'on place au bon endroit de la mémoire au bon moment OU tout recompiler à chaque installation d'application sur mon serveur et installer l'update depuis la carte SD_
- **ChatGPT** : Self-Explanatory
- **Emulateur GB** Would need external peripherals genre une manette
- **En général des petits jeux** : Snake, Tetris, Pong, etc.
- **Mail** : Client mail basique (SMTP IMAP), avec possibilité de push de notifications à la réception de nouveaux mails (via un service qui check régulièrement la boîte mail)
- **Notes** : Prendre des notes, les stocker localement, possibilité de les synchroniser avec un service en ligne (genre Google Keep)
- **Agenda** : Afficher un agenda, avec possibilité d'ajouter des événements, de les synchroniser avec Google Calendar par exemple
- **Maps** : Afficher une carte, possibilité de faire du GPS afficher sa position, tracer des itinéraires, etc. --> Besoin d'une implem carte SD pour stocker les données carto. 
- 


## D'un point de vue structurel 
- Faire qqch vis a vis des appllis qui s'entassent, faire un scrapper voire un truc automatique (script python) qui fait ca a chaque compil
- Avoir un vrai **pack d'icones**
- Priorité : Trouver une implémentation pour des applications, que ca soit en interprété ou en compilé...