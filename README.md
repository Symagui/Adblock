# Simple Proxy

### Description

This HTTP/HTTPS proxy is a simple proxy which can filter your navigation by skipping ads links for example, or any url containing one of the strings defined in specific files. (See "Help" section).

### Authors

This proxy has been made by Romaric Mollard and Guillaume Ruchot in April 2017.

### Install

Just *make* the sources then enter *./proxy -h*

### Quick start

To use the proxy and filter adverts (proxy on port 400) :
```
sudo ./proxy -p 400 mask.txt
```

### Help
```
SYNOPSIS
		(sudo) ./proxy [OPTION]... [FICHIER FILTRE, FICHIER FILTRE, ...]
OPTIONS
		-h
			Affichage de ce message.
		-p
			Modification du port du proxy.
		-q
			N'affiche aucun message dans la console autre que l'aide.
EXEMPLE
		sudo ./proxy -p 400 mask.txt
    Démarre un proxy sur le port 400 en utilisant les filtres du fichier mask.txt
```
