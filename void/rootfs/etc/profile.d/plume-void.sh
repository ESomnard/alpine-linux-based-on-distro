# Plume Void — session Wayland : les portails (partage d'écran, fichiers) savent quel bureau est utilisé
export XDG_CURRENT_DESKTOP=sway
export XDG_SESSION_TYPE=wayland
# HDR (plume-ecrans --hdr on) : sway doit utiliser le moteur de rendu Vulkan
[ -f "${XDG_CONFIG_HOME:-$HOME/.config}/plume/rendu-vulkan" ] && export WLR_RENDERER=vulkan
