# Projet ISY - Messagerie Instantanée UDP

## Introduction

Création d'un système de messagerie instantanée via programmation en C sous Linux.

## Installation

```bash
# Cloner le projet
git clone https://github.com/clementfvrl/projet_linux.git
cd projet_linux

# Installer xterm et les fonts nécessaires

# Debian / Ubuntu
sudo apt update
sudo apt install xterm xfonts-base xfonts-terminus fonts-dejavu

# Fedora / RHEL / AlmaLinux
sudo dnf install xterm xorg-x11-fonts-misc terminus-fonts
```

## Description

ISY est une application de chat centralisée basée sur le protocole UDP, développée en C pour les systèmes Unix/Linux. Elle utilise une architecture multi-processus et multi-sockets pour permettre des discussions de groupe en temps réel avec une interface scindée (Menu / Fenêtre de discussion).

## Fonctionnalités imposées par le professeur

- Créer et lister les groupes
- Supprimer un groupe (**Admin**)
- Rejoindre et quitter un groupe
- Envoyer un message dans un groupe
- Lire les messages d'un groupe
- Lister les membres d'un groupe
- Modérer les membres d'un groupe (**Admin**)
- Fusion des groupes de discussion (membres + messages, **Admin**)

## Fonctionnalité ajoutée par le groupe

- Chiffrement des messages
- Statistiques sur l'activité des membres du groupe

## Prérequis

Pour exécuter ce projet, vous avez besoin de :

- Système d'exploitation : Linux / Unix (ou WSL sous Windows).

- Compilateur : gcc.

- Terminal X11 : xterm est obligatoire car le client lance automatiquement une nouvelle fenêtre via la commande xterm.

```bash
# Installation des pré-requis

# Debian / Ubuntu
sudo apt update
sudo apt install build-essential xterm xfonts-base xfonts-terminus fonts-dejavu

# Fedora / RHEL / AlmaLinux
sudo dnf install make automake gcc gcc-c++ kernel-devel xterm xorg-x11-fonts-misc terminus-fonts
```

- Configurer l'adresse IP du serveur :

```bash
# Dans les fichiers `commun.h`, changez l'adresse IP du Serveur
# Remplacez par l'adresse ip de votre machine
define ISY_IP_SERVEUR "172.20.10.4"

# Pour connaître votre adresse IP, exécutez :

# Windows
ipconfig

# MacOS
ifconfig

# Linux
ip a
```

## Compilation

```bash
# Entrer dans le projet (si ce n'est pas déjà fait)
cd projet_linux

# Compiler le projet
make clean
make
```

## Utilisation

**1. Démarrage**

```bash
# A la racine du projet

# Démarrer le serveur
./bin/ServeurISY
```

Ensuite, lancez un ou plusieurs clients dans d'autres terminaux :

```bash
# A la racine du projet

# Démarrer le client
./bin/ClientISY
```

**2. Menu Principal : Une fois connecté avec votre pseudo**

1. Créer un groupe : Démarre une nouvelle salle sur le serveur.

2. Lister les groupes : Affiche les groupes disponibles et leurs ports.

3. Rejoindre un groupe : Connecte le client et ouvre la fenêtre de réception xterm.

4. Dialoguer : Permet d'envoyer des messages au groupe actif.

5. Quitter le groupe : Ferme la fenêtre de réception.

6. Supprimer un groupe : (Modérateur uniquement) Supprime le groupe et éjecte les membres.

7. Fusionner : (Modérateur uniquement) Fusionne deux groupes en un seul.

**3. Commandes Spéciales : Lorsque vous êtes dans le menu "Dialoguer" (Option 4), vous pouvez taper `cmd` pour entrer en mode commande**

- list : Afficher les membres connectés au groupe.

- stats : Afficher les statistiques d'activité des membres.

- ban <nom> : (Modérateur) Bannir un utilisateur du groupe.

- msg : Retourner au mode d'envoi de messages classique.

- quit : Quitter le mode dialogue, quittera le groupe et reviendra au menu principal.

### Structure des Fichiers

commun.h : Fichier d'en-tête commun. Contient les bibliothèques, les constantes (IP/Ports), la structure de message et l'algorithme de chiffrement César.

ServeurISY.c : Le processus central. Il gère l'annuaire, vérifie les connexions et fork les processus GroupeISY sur demande.

ClientISY.c : L'interface utilisateur. Il gère le menu textuel, envoie les requêtes et les messages (chiffrés) au serveur et fork le processus AffichageISY.

GroupeISY.c : Le processus de groupes de discussion. Il reçoit les messages, gère les statistiques, applique la modération et redistribue les messages aux membres.

AffichageISY.c : Le processus de réception. Il tourne dans une fenêtre xterm séparée, reçoit les messages UDP, les déchiffre et les affiche.

## Développement

```bash
# Je fais les modifications de mon code etc
git pull
git add .
git commit -m "mon message"
git push
```