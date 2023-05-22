# Présentation du projet

## Sujet

Ce projet est la réalisation du projet système de 2e année d'informatique de l'ENSEIRB-Matmeca proposé par Brice Goglin pour la promotion 2024 dont voici un [extrait du sujet](https://goglin.gitlabpages.inria.fr/enseirb-it202/) :

> Ce projet vise à construire une bibliothèque de threads en espace utilisateur. On va donc fournir une interface de programmation plus ou moins proche des pthreads, mais en les exécutant sur un seul thread noyau. Les intérêts sont :
>
> - Les coûts d'ordonnancement sont beaucoup plus faibles
> - Les politiques d'ordonnancement sont configurables
> - On peut enfin expérimenter le changement de contexte pour de vrai

## Arborescence

```txt
├── install
│   ├── bin     // Exécutables des tests
│   └── lib     // Notre bibliothèque de threads
├── src         // Code source de notre bibliothèque
├── tst         // Ensemble de tests pour notre
├── tests.csv   // Table de tests avec leurs arguments
├── graphs.py   // Script de mesure de performances
├── Makefile
└── README.md
```

# Installation

L'intégralité du dépôt est géré par `make` et son Makefile, que ce soit pour la compilation ou l'exécution des tests.

## Compilation basique

```sh
make
```

ou

```sh
make threads
```

Ces deux commandes permettent de compiler la bibliothèque ainsi que tous les tests en même temps.

## Compilation avec `pthread`

Il est possible de compiler les tests en remplaçant notre bibliothèque de threads utilisateurs par `pthread` :

```sh
make pthreads
```

## Options de compilation

- `make DEBUG=-DDEBUG` : Active le mode debut avec création de log dédiés
- `make DEADLOCK=-DALLOWDEADLOCK` : Désactive la détection des deadlocks sur les `thread_join`. Les tests 81 à 84 ne seront plus valide.
- `make PREEMPTION=-DPREEMPTION` : Active le support de la préemption (version alpha, créé des erreurs mémoires)

# Utilisation

## Choix des tests

Le `Makefile` de ce dépôt est conçu pour exécuter les tests recensés dans le fichier [`tests.csv`](./tests.csv) avec les arguments en deuxième colonne de ce fichier. Ainsi, il est possible en ajoutant, modifiant ou supprimant des lignes de choisir quelle batterie de test on souhait exécuter

## Exécution des tests et vérification mémoire

L'exécution de la batterie de tests de [`tests.csv`](./tests.csv) se fait à l'aide de la commande :

```sh
make check
```

Si l'on souhaite vérifier l'utilisation de la mémoire par les tests, on peut les exécuter par `valgrind` depuis la commande :

```sh
make valgrind
```

Il est enfin possible d'exécuter chaque test sans passer par `make`. Ceux-ci étant installé par défaut dans `./install/bin` lors de la compilation par `make`, il faudra les lancer par la commande :

```sh
LD_LIBRARY_PATH=./install/lib ./install/bin/<nom_du_test>
```

## Mesure de performances

Nous proposons en plus de la bibliothèque et nos tests un outil pour exécuter des mesures de performance à l'aide du script Python [graph.py](./graph.py). Il suffit alors de lancer

```sh
make graph
```

Ce script génère une série de graphes comparatif entre l'exécution des tests avec `libthread.so` et celle avec `pthread`

# Auteurs

Ce projet a été réalisé par l'équipe 2 du groupe de TD 4 de la promotion 2024 de la filière informatique de l'ENSEIRB-Matmeca, composé de :

- Sarah Mainguet : [smainguet001@bordeaux-inp.fr](mailto:smainguet001@bordeaux-inp.fr)
- Théodore Gigault : [tgigault@bordeaux-inp.fr](mailto:tgigault@bordeaux-inp.fr)
- Maxime Saland : [msaland@bordeaux-inp.fr](mailto:msaland@bordeaux-inp.fr)
- Mathieu Dupoux : [mdupoux@bordeaux-inp.fr](mailto:mdupoux@bordeaux-inp.fr)
