#!/usr/bin/env python3
"""Test de Plume Void dans QEMU, piloté par la console série.
1. démarre l'ISO live (noyau + initrd extraits, console=ttyS0), connexion root
2. installation automatique (install.sh --auto) sur le disque virtuel
3. redémarre sur le disque (BIOS), vérifie services, paquets Plume, sécurité, écran HUD
"""
import glob
import os
import re
import sys

import pexpect

ISO = sys.argv[1]
DISK = "/o/test-void/disk.qcow2"
PW = "plume-test-123"
CHIFFRE = os.environ.get("PLUME_TEST_CHIFFRE") == "1"
PHRASE = "phrase-test-456"
QEMU = "qemu-system-x86_64 -accel tcg,thread=multi -cpu max -m 3072 -smp 4 -nographic"
log = open("/o/test-void/vm.log", "w", encoding="utf-8", errors="replace", buffering=1)
counter = 0


def step(msg):
    print(f"\n### {msg}", flush=True)
    log.write(f"\n### {msg}\n")


def die(msg):
    step(f"ECHEC : {msg}")
    sys.exit(1)


class VM:
    def __init__(self, args):
        self.p = pexpect.spawn(f"{QEMU} {args}", encoding="utf-8", codec_errors="replace", timeout=300)
        self.p.logfile_read = log

    def expect(self, pats, timeout, what):
        try:
            return self.p.expect(pats, timeout=timeout)
        except pexpect.TIMEOUT:
            die(f"délai dépassé ({what})")
        except pexpect.EOF:
            die(f"la VM s'est arrêtée ({what})")

    def run(self, cmd, timeout=300):
        """Lance une commande ; renvoie (code de sortie, sortie)."""
        global counter
        counter += 1
        self.p.send(f'{cmd} ; echo "@@F""IN-{counter}-$?@@"\r')
        self.expect(rf"@@FIN-{counter}-(\d+)@@", timeout, cmd[:60])
        code = int(self.p.match.group(1))
        out = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", self.p.before)  # sans les couleurs
        return code, out

    def check(self, what, cmd, pattern=None, timeout=300):
        code, out = self.run(cmd, timeout)
        ok = code == 0 and (pattern is None or re.search(pattern, out) is not None)
        step(f"{'OK' if ok else 'ECHEC'} : {what}")
        return ok

    def login(self, user, pw):
        self.expect(r"login: ?$", 1800, "invite de connexion")
        self.p.send(f"{user}\r")
        self.expect("assword:", 120, "mot de passe")
        self.p.send(f"{pw}\r")
        self.expect([r"\$ ?$", r"# ?$"], 180, "invite du shell")
        self.run("export PS1='$ ' TERM=dumb; stty -echo cols 200", 60)


# PLUME_TEST_PHASE2=1 : disque déjà installé, on ne refait que les vérifications
PHASE2 = os.environ.get("PLUME_TEST_PHASE2") == "1"

# ============================ live + installation ============================
if not PHASE2:
    cfg = open("/tmp/isoboot/isolinux/isolinux.cfg").read()
    m = re.search(r"APPEND initrd=\S+\s+(.*)", cfg)
    kargs = re.sub(r"\s+", " ", m.group(1)).strip()
    step(f"Phase 1 : ISO live (noyau : {kargs[:120]})")
    vm = VM(f"-no-reboot -cdrom {ISO} -kernel /tmp/isoboot/vmlinuz -initrd /tmp/isoboot/initrd "
            f"-append '{kargs} console=ttyS0,115200' "
            f"-drive file={DISK},if=virtio -nic user,model=virtio-net-pci")
    vm.login("root", "voidlinux")
    step("Live OK")
    vm.check("utilisateur live plume (groupes du bureau)", "id plume", r"_seatd")
    vm.check("paquets Plume dans le live", "xbps-query plume-bureau", r"pkgver: plume-bureau")
    conf = (f'DISQUE=/dev/vda\nUTILISATEUR=Test.Plume\nNOM_COMPLET="Élève Test"\nMOT_DE_PASSE={PW}\nREDEMARRER=non\nNOYAU_EXTRA="console=ttyS0,115200"\n')
    if CHIFFRE:
        conf += f"CHIFFREMENT=oui\nPHRASE_CHIFFREMENT={PHRASE}\n"
    vm.run(f"printf '{conf}' > /tmp/auto.conf")
    step("Installation automatique")
    code, out = vm.run("sh /etc/plume/install.sh --auto /tmp/auto.conf 2>&1 | tail -15", 5400)
    if "Installation termin" not in out:
        step("Diagnostic de l'échec")
        vm.run("mount /dev/vda3 /mnt && mount /dev/vda2 /mnt/boot/efi && mount -t proc proc /mnt/proc "
               "&& mount --rbind /sys /mnt/sys && mount --rbind /dev /mnt/dev; ls /mnt/boot /mnt/boot/efi", 120)
        vm.run("chroot /mnt grub-install --target=i386-pc /dev/vda 2>&1 | tail -5", 600)
        vm.run("chroot /mnt grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=Plume --removable 2>&1 | tail -5", 600)
        vm.run("chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5; grep -c initramfs /mnt/boot/grub/grub.cfg", 600)
        die("installation : " + out[-600:])
    step("Installation terminée")
    vm.run("poweroff", 120) if False else vm.p.send("poweroff\r")
    try:
        vm.p.expect(pexpect.EOF, timeout=300)
    except pexpect.TIMEOUT:
        vm.p.terminate(force=True)

# ============================== système installé =============================
step("Phase 2 : démarrage sur le disque (BIOS)")
vm = VM(f"-no-reboot -drive file={DISK},if=virtio -nic user,model=virtio-net-pci "
        "-device qemu-xhci -drive if=none,id=cle,format=raw,file=/tmp/usb.img -device usb-storage,drive=cle,removable=on")
if CHIFFRE:
    vm.expect(r"[Pp]assword|[Pp]assphrase", 900, "demande de la phrase secrète")
    vm.p.send(f"{PHRASE}\r")
    step("Phrase secrète acceptée")
vm.login("Test.Plume", PW)
step("Connexion de l'utilisateur OK")
ok = True
ok &= vm.check("glibc", "ldd --version | head -1", r"GNU libc|GLIBC")
ok &= vm.check("11 composants Plume installés", "xbps-query -l | grep -c ' plume-'", r"\b1[01]\b")
for s in ("dbus", "seatd", "greetd", "nftables", "iwd", "bluetoothd", "zramen", "polkitd", "cupsd", "avahi-daemon", "power-profiles-daemon"):
    ok &= vm.check(f"service {s}", f"sudo -n true 2>/dev/null; echo {PW} | sudo -S sv status {s}", r"run:")
ok &= vm.check("écran de connexion plume-hud (greetd + cage)", "pgrep -af 'plume-hud --greeter'", r"plume-hud")
ok &= vm.check("pare-feu actif", f"echo {PW} | sudo -S nft list chain inet filter input", r"policy drop")
ok &= vm.check("AppArmor actif", "cat /sys/module/apparmor/parameters/enabled", r"Y")
ok &= vm.check("sysctl Plume", "sysctl -n kernel.kptr_restrict", r"[12]")
ok &= vm.check("plume-maj --verifier (sudo sans mot de passe)",
               "sudo -n /usr/bin/plume-maj --verifier && cat /var/lib/plume/maj-dispo", r"\d")
ok &= vm.check("apt traduit vers xbps", "apt search micro | head -3", r"micro")
ok &= vm.check("plume-parefeu", f"echo {PW} | sudo -S plume-parefeu", r"ACTIF")
ok &= vm.check("Logithèque installée", "command -v plume-logitheque", r"plume-logitheque")
ok &= vm.check("police JetBrains Mono", "fc-list | grep -c JetBrains", r"[1-9]")
ok &= vm.check("Internet", f"echo {PW} | sudo -S xbps-install -S >/dev/null && echo net-ok", r"net-ok", 120)
if CHIFFRE:
    ok &= vm.check("racine chiffrée", "lsblk -o TYPE,MOUNTPOINT | grep crypt", r"crypt")
# AppArmor : profils Plume chargés et vraiment appliqués
ok &= vm.check("profils AppArmor Plume chargés (enforce)",
               f"echo {PW} | sudo -S cat /sys/kernel/security/apparmor/profiles | grep -c '^plume-.*(enforce)'", r"\b4\b")
vm.run("mkdir -p ~/.ssh && echo secret > ~/.ssh/id_test && echo doc > ~/doc.txt && cp /usr/bin/true ~/prog")
ok &= vm.check("AppArmor : Firefox lit tes documents mais pas tes clés SSH",
               "aa-exec -p plume-firefox -- cat ~/doc.txt && ! aa-exec -p plume-firefox -- cat ~/.ssh/id_test && echo aa-ok", r"aa-ok")
ok &= vm.check("AppArmor : pas d'exécution d'un fichier téléchargé",
               "~/prog && ! aa-exec -p plume-firefox -- sh -c ~/prog && echo exec-bloque", r"exec-bloque")
ok &= vm.check("AppArmor : pas de démarrage automatique (.bashrc)",
               "! aa-exec -p plume-mpv -- sh -c 'echo x >> ~/.bashrc' && echo persist-bloque", r"persist-bloque")
ok &= vm.check("AppArmor : Zathura (PDF) sans Internet",
               "wget -q -O /dev/null http://repo-default.voidlinux.org/ && ! aa-exec -p plume-zathura -- wget -q -O /dev/null http://repo-default.voidlinux.org/ && echo net-bloque",
               r"net-bloque", 120)
ok &= vm.check("Firefox démarre confiné (plume-firefox)",
               "(firefox --headless --no-remote about:blank >/dev/null 2>&1 &) ; sleep 60 ; "
               "cat /proc/$(pgrep -o -f /usr/lib/firefox/firefox)/attr/apparmor/current ; pkill -f /usr/lib/firefox/firefox",
               r"plume-firefox \(enforce\)", 240)
ok &= vm.check("AppArmor : mpv ne lit pas le profil Firefox (cookies, mots de passe)",
               "P=$(ls -d ~/.config/mozilla/firefox ~/.mozilla/firefox 2>/dev/null | head -1); ls \"$P\" >/dev/null && "
               "! aa-exec -p plume-mpv -- ls \"$P\" 2>/dev/null && echo cookies-proteges", r"cookies-proteges")
# clé USB : montée par l'utilisateur sans mot de passe (udisks2 + règle polkit du groupe storage)
ok &= vm.check("clé USB montée sans mot de passe (udisks2 + polkit)",
               "udisksctl mount --no-user-interaction -b /dev/disk/by-label/PLUMEUSB && "
               "cat /run/media/$(id -un)/PLUMEUSB/bonjour.txt && "
               "udisksctl unmount --no-user-interaction -b /dev/disk/by-label/PLUMEUSB", r"cle-usb-lue", 180)
ok &= vm.check("polkit : clé USB permise, disque interne et formatage refusés sans mot de passe",
               "pkcheck --action-id org.freedesktop.udisks2.filesystem-mount --process $$ && "
               "! pkcheck --action-id org.freedesktop.udisks2.filesystem-mount-system --process $$ && "
               "! pkcheck --action-id org.freedesktop.udisks2.modify-device --process $$ && echo polkit-ok", r"polkit-ok", 120)
# session sway sans écran (headless) : portails, plume-ecrans
SESSION = """
export XDG_RUNTIME_DIR=/tmp/rt-$(id -u); mkdir -p -m700 "$XDG_RUNTIME_DIR"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
dbus-run-session sh -c '
  pipewire >/tmp/pipewire.log 2>&1 &
  printf "set \\$mod Mod4\ninclude /etc/sway/config.d/50-plume-void.conf\n" > /tmp/sway-test.conf
  sway -c /tmp/sway-test.conf >/tmp/sway.log 2>&1 &
  sleep 30
  export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock | head -1)
  echo "bureau=$XDG_CURRENT_DESKTOP"
  P="--session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.DBus.Properties.Get"
  gdbus call $P org.freedesktop.portal.ScreenCast version && echo portail-ecran-ok
  gdbus call $P org.freedesktop.portal.FileChooser version && echo portail-fichiers-ok
  plume-ecrans --enregistrer && swaymsg reload && grep -q "^output" ~/.config/sway/ecrans.conf && echo ecrans-ok
  plume-ecrans --liste
  plume-accessibilite loupe 2 >/dev/null; [ "$(swaymsg -t get_outputs -r | jq ".[0].scale == 2")" = true ] && echo loupe-ok
  plume-accessibilite loupe 1 >/dev/null
  plume-ecrans --hdr on | grep -q Vulkan && [ -f ~/.config/plume/rendu-vulkan ] && plume-ecrans --hdr off >/dev/null && [ ! -f ~/.config/plume/rendu-vulkan ] && echo hdr-ok
  sway -C -c /tmp/sway-test.conf >/tmp/sway-check.log 2>&1 && echo config-sway-ok
  plume-raccourcis & sleep 15
  swaymsg -t get_tree -r | jq -e "[.. | objects | select(.name? == \\"Raccourcis Plume\\" and .type == \\"floating_con\\")] | length > 0" >/dev/null && echo panneau-ouvert-ok
  plume-raccourcis; sleep 3
  swaymsg -t get_tree -r | jq -e "[.. | objects | select(.name? == \\"Raccourcis Plume\\")] | length == 0" >/dev/null && echo panneau-ferme-ok
  plume-raccourcis --faire b2; sleep 1
  [ "$(swaymsg -t get_workspaces -r | jq -r ".[] | select(.focused) | .name")" = 2 ] && echo bouton-raccourci-ok
  swaymsg exit
'
"""
import base64
b64 = base64.b64encode(SESSION.encode()).decode()
code, out = vm.run(f"echo {b64} | base64 -d > /tmp/session.sh; sh /tmp/session.sh 2>&1 | grep -v 'I/O error' | tail -25", 400)
step("OK : portail partage d'écran (wlr)" if "portail-ecran-ok" in out else "ECHEC : portail partage d'écran (wlr)")
step("OK : portail fichiers (gtk)" if "portail-fichiers-ok" in out else "ECHEC : portail fichiers (gtk)")
step("OK : plume-ecrans enregistre et sway relit les réglages" if "ecrans-ok" in out else "ECHEC : plume-ecrans")
step("OK : loupe (zoom ×2 puis ×1)" if "loupe-ok" in out else "ECHEC : loupe")
step("OK : HDR (passage au rendu Vulkan, puis retour)" if "hdr-ok" in out else "ECHEC : HDR")
step("OK : configuration sway valide (raccourcis Alt+F4, Super+Ctrl+flèches, Super+F1)" if "config-sway-ok" in out else "ECHEC : configuration sway")
step("OK : panneau Raccourcis ouvert (fenêtre libre)" if "panneau-ouvert-ok" in out else "ECHEC : panneau Raccourcis ouvert")
step("OK : panneau Raccourcis refermé (même raccourci)" if "panneau-ferme-ok" in out else "ECHEC : panneau Raccourcis refermé")
step("OK : bouton du panneau (aller au bureau 2)" if "bouton-raccourci-ok" in out else "ECHEC : bouton du panneau Raccourcis")
ok &= all(m in out for m in ("portail-ecran-ok", "portail-fichiers-ok", "ecrans-ok", "loupe-ok", "hdr-ok",
                             "config-sway-ok", "panneau-ouvert-ok", "panneau-ferme-ok", "bouton-raccourci-ok"))
if "config-sway-ok" not in out:
    vm.run("tail -8 /tmp/sway-check.log")
ok &= vm.check("logos Plume et icônes du bureau installés",
               "ls /usr/share/icons/hicolor/scalable/apps/plume-wifi.svg && command -v plume-icones-bureau && "
               "grep -q '^Icon=plume-wifi$' /usr/share/applications/plume-wifi.desktop && echo logos-ok", r"logos-ok")
ok &= vm.check("dock : plus de bouton fermer ni raccourcis ; plume-linux help montre les raccourcis",
               "! grep -q 'swaymsg kill\\|plume-raccourcis' /etc/plume/dock.conf && "
               "plume-linux help | grep -q 'Alt+F4' && plume-linux help | grep -q 'Aller au bureau 1 à 10' && echo dock-ok", r"dock-ok")
if "portail-ecran-ok" not in out:
    vm.run("tail -5 /tmp/pipewire.log; ls /usr/share/xdg-desktop-portal/portals/ /etc/xdg/xdg-desktop-portal/")
# imprimantes, scanners, énergie, firmwares, VPN, wifi public
ok &= vm.check("imprimantes : CUPS prêt, compte dans lpadmin",
               "lpstat -r && id -nG | tr ' ' '\\n' | grep -x lpadmin", r"lpadmin")
ok &= vm.check("scanners : SANE + réseau (airscan)", "scanimage --version && grep -qx airscan /etc/sane.d/dll.d/* /etc/sane.d/dll.conf && echo airscan-ok", r"airscan-ok")
ok &= vm.check("énergie : mode économie sans mot de passe (polkit)",
               "plume-batterie economie && powerprofilesctl get && plume-batterie equilibre", r"power-saver")
# fwupd : en VM émulée, son démarrage dépasse le délai de dbus (25 s) ; on le lance d'abord
vm.run(f"echo {PW} | sudo -S sh -c 'setsid -f /usr/libexec/fwupd/fwupd </dev/null >/dev/null 2>&1'; "
       "for i in $(seq 30); do sudo fwupdmgr get-devices --json >/dev/null 2>&1 && break; sleep 10; done", 400)
ok &= vm.check("firmwares du PC : fwupd répond", f"echo {PW} | sudo -S fwupdmgr get-devices --json 2>/dev/null | head -c 300", r"Devices", 200)
ok &= vm.check("pilotes : pas de carte NVIDIA détectée (VM)", "plume-pilotes", r"Pas de carte NVIDIA")
vm.run("umask 077; printf '[Interface]\\nPrivateKey = %s\\nAddress = 10.99.0.2/24\\n[Peer]\\nPublicKey = %s\\n"
       "Endpoint = 192.0.2.1:51820\\nAllowedIPs = 10.99.0.0/24\\n' $(wg genkey) $(wg genkey | wg pubkey) > /tmp/essai.conf")
ok &= vm.check("VPN WireGuard : ajout, connexion, déconnexion (plume-vpn)",
               f"echo {PW} | sudo -S plume-vpn ajouter /tmp/essai.conf && sudo plume-vpn connecter essai && "
               "ip -br link show essai && plume-vpn etat && sudo plume-vpn deconnecter && ! ip link show essai 2>/dev/null && echo vpn-ok",
               r"vpn-ok", 120)
ok &= vm.check("wifi public : détection du portail (réseau normal = ok)", "plume-portail", r"Réseau : ok", 60)
ok &= vm.check("pare-feu : découverte des imprimantes (mDNS) ouverte",
               f"echo {PW} | sudo -S nft list chain inet filter input | grep -c 5353", r"\b2\b")
ok &= vm.check("VS Code natif dans la Logithèque",
               "grep -c '| vscode |' /usr/share/plume/logitheque.txt && xbps-query -R vscode | grep -m1 pkgver", r"pkgver: vscode")
ok &= vm.check("Écrans dans la grille des applications", "grep -c '^Name=Écrans' /usr/share/applications/plume-ecrans.desktop", r"1")
# capture de l'écran de connexion (cage accepte grim)
vm.run(f"echo {PW} | sudo -S sh -c '"
       "XDG_RUNTIME_DIR=/var/lib/_greeter/runtime WAYLAND_DISPLAY=$(ls /var/lib/_greeter/runtime | grep -m1 ^wayland-[0-9]$) "
       "grim /tmp/g.png' ; echo ===B64START greeter=== ; base64 /tmp/g.png ; echo ===B64END===", 300)
vm.run("df -h / | tail -1; free -m | sed -n 2p")
# slim-firmware (en dernier : allège le disque de test)
ok &= vm.check("slim-firmware : firmwares allégés, mises à jour xbps toujours possibles",
               "a=$(du -sm /usr/lib/firmware | cut -f1); "
               f"echo {PW} | sudo -S sh /etc/plume/slim-firmware.sh --oui >/tmp/slim.log 2>&1; "
               "b=$(du -sm /usr/lib/firmware | cut -f1); echo \"firmwares : $a Mo -> $b Mo\"; "
               f"{{ [ $b -lt $a ] || [ $b -le 2 ]; }} && [ -f /etc/xbps.d/90-plume-firmware.conf ] && echo {PW} | sudo -S xbps-install -Sun >/dev/null && echo slim-ok",
               r"slim-ok", 900)
vm.run("tail -4 /tmp/slim.log")
ok &= vm.check("accessibilité : Orca, voix française, clavier à l'écran",
               "command -v orca wvkbd-mobintl && grep -q '^DefaultLanguage \"fr\"' /etc/speech-dispatcher/speechd.conf && echo a11y-ok", r"a11y-ok")
# sauvegarde sur la clé USB (FAT)
ok &= vm.check("sauvegarde des fichiers sur un disque externe (plume-sauvegarde)",
               "{ udisksctl mount --no-user-interaction -b /dev/disk/by-label/PLUMEUSB >/dev/null 2>&1 || mountpoint -q /run/media/$(id -un)/PLUMEUSB; } && "
               "plume-sauvegarde --disque /run/media/$(id -un)/PLUMEUSB && plume-sauvegarde maintenant && "
               "ls /run/media/$(id -un)/PLUMEUSB/Plume-sauvegardes/$(id -un)/*/doc.txt && "
               "udisksctl unmount --no-user-interaction -b /dev/disk/by-label/PLUMEUSB", r"doc\.txt", 300)
# retour en arrière : instantané, on casse le système (logiciel retiré, fichier ajouté), on restaure
ok &= vm.check("système sur btrfs (sous-volume @)", "findmnt -no FSTYPE,FSROOT /", r"btrfs\s+/@")
code, out = vm.run(f"echo {PW} | sudo -S plume-retour creer 'test du retour'", 120)
m = re.search(r"Instantané (\d+) créé", out)
ok &= m is not None
step("OK : instantané créé" if m else "ECHEC : instantané")
if m:
    vm.run(f"echo {PW} | sudo -S xbps-remove -y micro >/dev/null; sudo touch /etc/plume-marqueur; echo apres > ~/apres.txt; command -v micro || echo micro-retire", 300)
    ok &= vm.check("retour en arrière (restauration)", f"echo {PW} | sudo -S plume-retour restaurer {m.group(1)} --oui", r"Système remis", 300)
    vm.p.send(f"echo {PW} | sudo -S reboot\r")
    try:
        vm.p.expect(pexpect.EOF, timeout=300)
    except pexpect.TIMEOUT:
        vm.p.terminate(force=True)
    # ============================ après le retour en arrière ============================
    step("Phase 3 : redémarrage après le retour en arrière")
    vm = VM(f"-no-reboot -drive file={DISK},if=virtio -nic user,model=virtio-net-pci")
    if CHIFFRE:
        vm.expect(r"[Pp]assword|[Pp]assphrase", 900, "demande de la phrase secrète")
        vm.p.send(f"{PHRASE}\r")
    vm.login("Test.Plume", PW)
    ok &= vm.check("après retour : logiciel retiré revenu, fichier système ajouté disparu",
                   "command -v micro && [ ! -e /etc/plume-marqueur ] && echo systeme-revenu", r"systeme-revenu")
    ok &= vm.check("après retour : fichiers personnels intacts (/home)", "cat ~/doc.txt ~/apres.txt", r"doc\s+apres")
    ok &= vm.check("après retour : ancien système nettoyé au prochain instantané",
                   f"echo {PW} | sudo -S plume-retour creer suivant >/dev/null && [ \"$(sudo plume-retour | grep -c ancien)\" = 0 ] && echo ancien-nettoye", r"ancien-nettoye")
step("SUCCES : tous les tests sont passés" if ok else "ECHEC : au moins un test a échoué")
vm.p.send(f"echo {PW} | sudo -S poweroff\r")
