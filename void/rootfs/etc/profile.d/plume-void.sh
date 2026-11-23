# Plume Void — session Wayland : les portails (partage d'écran, fichiers) savent quel bureau est utilisé
export XDG_CURRENT_DESKTOP=sway
export XDG_SESSION_TYPE=wayland
# HDR (plume-ecrans --hdr on) : sway doit utiliser le moteur de rendu Vulkan
[ -f "${XDG_CONFIG_HOME:-$HOME/.config}/plume/rendu-vulkan" ] && export WLR_RENDERER=vulkan
# pastilles rouge / jaune / verte dans le gestionnaire de fichiers (paquet plume-pastilles-gtk ;
# le module ne fait rien dans les autres applis GTK)
case ":${GTK3_MODULES:-}:" in *:plume-pastilles:*) ;; *) export GTK3_MODULES="${GTK3_MODULES:+$GTK3_MODULES:}plume-pastilles" ;; esac
