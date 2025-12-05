# Projet ISY - Messagerie Instantanée UDP

## Introduction

Création d'un système de messagerie instantanée via programmation en C sous Linux.

## Installation

```bash
git clone https://github.com/clementfvrl/projet_linux.git
cd projet_linux
```

## Utilisation

```bash
# Je fais les modifications de mon code etc
git pull
git add .
git commit -m "mon message"
git push
```

## Description

ISY est une application de chat centralisée basée sur le protocole UDP, développée en C pour les systèmes Unix/Linux. Elle utilise une architecture multi-processus et multi-sockets pour permettre des discussions de groupe en temps réel avec une interface scindée (Menu / Fenêtre de discussion).

### Fonctionnalités imposées par le professeur

- Créer et lister les groupes
- Supprimer un groupe (**Admin**)
- Rejoindre et quitter un groupe
- Envoyer un message dans un groupe
- Lire les messages d'un groupe
- Lister les membres d'un groupe
- Modérer les membres d'un groupe (**Admin**)
- Chiffrement des messages
- Fusion des groupes de discussion (membres + messages, **Admin**)

### Fonctionnalité ajoutée par le groupe

- Statistiques sur l'activité des membres du groupe

### Prérequis

Pour exécuter ce projet, vous avez besoin de :

- Système d'exploitation : Linux / Unix (ou WSL sous Windows).

- Compilateur : gcc.

- Terminal X11 : xterm est obligatoire car le client lance automatiquement une nouvelle fenêtre via la commande xterm.

-- Installation (Debian/Ubuntu) : 

```bash
sudo apt install xterm
```

### Compilation

dans projet_linux : 

```bash

make clean
make
```

### Utilisation

1. Démarrage 

```bash

./bin/ServeurISY
```

Ensuite, lancez un ou plusieurs clients dans d'autres terminaux :

```bash

./bin/ClientISY
```

2. Menu Principal
Une fois connecté avec votre pseudo :

    1. Créer un groupe : Démarre une nouvelle salle sur le serveur.

    2. Lister les groupes : Affiche les groupes disponibles et leurs ports.

    3. Rejoindre un groupe : Connecte le client et ouvre la fenêtre de réception xterm.

    4. Dialoguer : Permet d'envoyer des messages au groupe actif.

    5. Quitter le groupe : Ferme la fenêtre de réception.

    6. Supprimer un groupe : (Modérateur uniquement) Supprime le groupe et éjecte les membres.

    7. Fusionner : (Modérateur uniquement) Fusionne deux groupes en un seul.

3. Commandes Spéciales (Chat)
Lorsque vous êtes dans le mode "Dialoguer" (Option 4), vous pouvez taper cmd pour entrer en mode commande :

- list : Affiche les membres connectés au groupe.

- stats : Affiche les statistiques d'activité des membres.

- ban <nom> : (Modérateur) Bannit un utilisateur du groupe.

- delete <nom> : (Modérateur) Exclut un utilisateur.

- msg : Retourner au mode d'envoi de messages classique.

- quit : Quitter le mode dialogue, quitte le groupe, et revenir au menu.

#### Structure des Fichiers

commun.h : Fichier d'en-tête commun. Contient les bibliothèques, les constantes (IP/Ports), la structure MessageISY et l'algorithme de chiffrement César.

ServeurISY.c : Le processus central. Il gère l'annuaire, vérifie les connexions et fork les processus GroupeISY sur demande.

ClientISY.c : L'interface utilisateur. Il gère le menu textuel, envoie les requêtes au serveur et fork le processus AffichageISY.

GroupeISY.c : Le processus de salle. Il reçoit les messages, gère les statistiques, applique la modération et redistribue les messages aux membres.

AffichageISY.c : Le processus de réception. Il tourne dans une fenêtre xterm séparée, reçoit les messages UDP, les déchiffre et les affiche.

#### Architecture Technique
Extrait de code

graph TD
    User[Utilisateur] --> Client[ClientISY (Menu)]
    Client -- Requêtes (CRG, JNG...) --> Serveur[ServeurISY :8000]
    
    Serveur -- fork() --> Groupe[GroupeISY :PortX]
    Client -- fork() --> Affiche[AffichageISY (xterm)]
    
    Client -- Envoi MSG (UDP) --> Groupe
    Groupe -- Diffusion MSG (UDP) --> Affiche

