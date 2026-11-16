# Plume — commandes maison (apt, apt-get, apt-cache) disponibles dès la connexion
case ":$PATH:" in
*:/etc/plume/bin:*) ;;
*) export PATH="$PATH:/etc/plume/bin" ;;
esac
