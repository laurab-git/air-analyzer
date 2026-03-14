# Corrections de bugs - Air Analyzer

## Bugs corrigés

### 1. **Bug critique : Variables de navigation non partagées**
**Problème** : Les variables `currentViewIdx` et `lastViewSwitch` étaient déclarées comme `static` à l'intérieur de DEUX fonctions différentes (`updateDisplayCycle()` et `nextDisplayView()`). Elles ne se partageaient donc PAS, ce qui faisait que le changement de vue via bouton était écrasé 250ms plus tard.

**Solution** : Déplacé les variables au niveau du fichier (ligne 33-35 de display.cpp) pour qu'elles soient partagées entre toutes les fonctions.

### 2. **Bug : Changement de luminosité sans effet**
**Problème** : Le double appui pour changer la luminosité modifiait `manualBrightness`, mais si on n'était pas en mode MANUAL, ça n'avait aucun effet visible.

**Solution** : Le double appui passe maintenant automatiquement en mode MANUAL avant de changer la luminosité.

### 3. **Bug : Absence de debug MQTT**
**Problème** : Impossible de savoir si les commandes MQTT étaient reçues sans connecter en USB.

**Solution** : Ajout de messages Serial.printf() pour toutes les commandes MQTT reçues.

### 4. **Bug : Debug bouton manquant**
**Problème** : Impossible de savoir si le bouton était correctement initialisé.

**Solution** : Ajout d'un test au démarrage qui affiche l'état initial du bouton.

### 5. **Amélioration : Messages d'erreur MQTT Power**
**Problème** : Le switch power ne fonctionnait pas en mode AUTO/OFF sans explication.

**Solution** : Ajout d'un message explicite "Impossible - pas en mode MANUAL".

## Comment tester après compilation

### Test 1 : Vérifier les logs au démarrage (via USB)
```
Bouton initialisé sur GPIO 9, état: HIGH (non appuyé)
```

### Test 2 : Tester le bouton physique
1. **Appui court** : La vue devrait changer immédiatement
2. **Double appui** : Devrait passer en mode MANUAL et changer la luminosité
3. **Appui long** : En mode MANUAL, allume/éteint l'écran

### Test 3 : Tester MQTT depuis Home Assistant
1. Passer en mode MANUAL → Vérifier le log "MQTT: Passage en mode MANUAL"
2. Changer luminosité → Vérifier le log "MQTT: Luminosité -> 120"
3. Essayer power ON/OFF → Devrait fonctionner uniquement en mode MANUAL

### Test 4 : Vérifier la synchronisation
1. Changer de vue avec le bouton (appui court)
2. Attendre 5 secondes → La vue NE devrait PAS revenir automatiquement
3. Attendre le timeout → La vue devrait alors passer à la suivante dans le cycle

## GPIO alternatifs si GPIO 9 ne fonctionne pas

La configuration actuelle utilise GPIO 9 (`BUTTON_PIN 9` dans `config.h`). GPIO 8 est un strapping pin ESP32-C3 qui peut causer des conflits au boot — ne pas l'utiliser.

Si GPIO 9 pose problème, voici des alternatives :

### Option 1 : GPIO 18
```cpp
#define BUTTON_PIN 18
```

### Option 2 : GPIO 19
```cpp
#define BUTTON_PIN 19
```

**À NE PAS UTILISER** : GPIO 8 (strapping pin, conflit au boot), GPIO 10-17 (réservés pour le flash SPI)

## Checklist de déploiement

- [ ] Compiler et téléverser le nouveau firmware
- [ ] Se connecter en USB pour voir les logs de démarrage
- [ ] Vérifier que le bouton est bien détecté (log "Bouton initialisé")
- [ ] Tester l'appui court (changement de vue)
- [ ] Tester le double appui (changement luminosité)
- [ ] Tester l'appui long (toggle power en mode manuel)
- [ ] Tester les commandes MQTT depuis Home Assistant
- [ ] Vérifier que les changements sont persistants
- [ ] Déconnecter USB et vérifier en mode OTA

## Si ça ne fonctionne toujours pas

1. **Connectez-vous en USB** et regardez les logs
2. **Vérifiez MQTT** : Dans Home Assistant → Outils dev → MQTT → S'abonner à `air_analyzer/#`
3. **Testez manuellement** : Publiez sur `air_analyzer/display/mode/set` avec payload `manual`
4. **Vérifiez le bouton** : Le log montre-t-il "HIGH" ou "LOW" au démarrage ?
5. **GPIO alternatif** : Si le bouton ne répond pas, essayez GPIO 18 ou 19

## Temps de réponse attendus

- **Bouton physique** : < 100ms
- **Commande MQTT** : < 500ms
- **Changement auto de vue** : 5-10 secondes selon la vue
