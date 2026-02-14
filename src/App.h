#ifndef APP_H
#define APP_H

#include <lvgl.h>

/**
 * Classe de base pour toutes les applications
 */
class App {
public:
    virtual ~App() {}

    // Appelé au lancement de l'app : Créez vos boutons/labels ici
    virtual void start(lv_obj_t* parent) = 0;

    // Appelé à chaque tour de boucle (pour les animations ou la logique)
    virtual void update() {}

    // Appelé quand on quitte l'app (nettoyage)
    virtual void stop() {}

    // Appelé à chaque tour de boucle du coeur 1 (pour les requêtes réseau)
    virtual void update1() {}
};

#endif