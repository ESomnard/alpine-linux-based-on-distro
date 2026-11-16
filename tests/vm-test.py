#!/usr/bin/env python3
"""Test complet de Plume dans une VM QEMU, piloté par la console série.

Lancé par tests/vm-test.sh dans un conteneur Alpine (python3 + py3-pexpect + qemu).
1. démarre l'ISO live et lance /etc/plume/install.sh (réponses automatiques)
2. redémarre sur le disque, lance desktop.sh, redémarre
3. vérifie services, paquets, sway (capture d'écran), pinyin, puis slim-firmware.sh
"""
import os
import re
import sys

import pexpect

ISO = sys.argv[1]
DISK = "/o/test/disk.qcow2"
PW = "plume-test-123"
# PLUME_TEST_CHIFFRE=1 : installation sur disque chiffré (LUKS), phrase tapée au démarrage, puis arrêt
CHIFFRE = os.environ.get("PLUME_TEST_CHIFFRE") == "1"
PHRASE = "phrase-test-456"
QEMU = "qemu-system-x86_64 -accel tcg,thread=multi -cpu max -m 2048 -smp 4 -nographic"

log = open("/o/test/vm.log", "w", encoding="utf-8", errors="replace", buffering=1)
counter = 0


def step(msg):
    print(f"\n### {msg}", flush=True)
    log.write(f"\n### {msg}\n")


def die(msg):
    step(f"ECHEC : {msg}")
    sys.exit(1)


class VM:
    def __init__(self, args):
        self.p = pexpect.spawn(f"{QEMU} {args}", encoding="utf-8",
                               codec_errors="replace", timeout=300)
        self.p.logfile_read = log

    def expect(self, patterns, timeout, what):
        try:
            return self.p.expect(patterns, timeout=timeout)
        except pexpect.TIMEOUT:
            die(f"délai dépassé ({what})")
        except pexpect.EOF:
            die(f"la VM s'est arrêtée ({what})")

    def send(self, text):
        self.p.send(text)

    def run(self, cmd, timeout=300):
        """Lance une commande et attend le marqueur de fin.
        Le marqueur est tapé F""IN pour que l'écho de la commande ne corresponde pas."""
        global counter
        counter += 1
        self.send(f'{cmd} ; echo @@F""IN-{counter}@@\r')
        self.expect(re.escape(f"@@FIN-{counter}@@"), timeout, cmd[:60])
        self.prompt()

    def prompt(self, timeout=120):
        """Chaque invite du shell BusyBox envoie ESC[6n (position du curseur) et attend
        la réponse avant d'accepter la saisie : on répond comme un vrai terminal."""
        self.expect(r"\x1b\[6n", timeout, "invite du shell")
        self.send("\x1b[24;1R")

    def run_interactive(self, cmd, answers, timeout):
        """Commande qui pose des questions : answers = [(regex, réponse), ...]"""
        global counter
        counter += 1
        marker = re.escape(f"@@FIN-{counter}@@")
        self.send(f'{cmd} ; echo @@F""IN-{counter}@@\r')
        pats = [marker] + [a[0] for a in answers]
        while True:
            i = self.expect(pats, timeout, cmd[:60])
            if i == 0:
                self.prompt()
                return
            reply = answers[i - 1][1]
            if callable(reply):
                reply = reply(self.p.match.group(0))
            if reply is not None:
                self.send(reply)

    def login(self, user):
        self.expect("login:", 900, "invite de connexion")
        self.send(f"{user}\r")
        if user != "root":
            self.expect("assword:", 120, "mot de passe")
            self.send(f"{PW}\r")
        self.prompt(180)
        self.run("echo shell-pret", 60)

    def wait_eof(self, timeout=180):
        self.expect(pexpect.EOF, timeout, "arrêt de la VM")


# ===================== Phase 1 : ISO live + installation =====================
step("Phase 1 : démarrage de l'ISO live")
vm = VM(f"-no-reboot -cdrom {ISO} -kernel /tmp/vmlinuz -initrd /tmp/initramfs "
        f"-append 'modules=loop,squashfs,sd-mod,usb-storage console=ttyS0,115200' "
        f"-drive file={DISK},if=virtio -nic user,model=virtio-net-pci")
vm.login("root")
step("Live OK, lancement de install.sh")

if os.environ.get("PLUME_TEST_INTERACTIF") == "1":
    iface = {"done": False}


    def iface_answer(_):
        if iface["done"]:
            return "done\r"
        iface["done"] = True
        return "eth0\r"


    vm.run_interactive("sh /etc/plume/install.sh", [
        (r"Disposition clavier[^:]*:", "\r"),
        (r"Fuseau horaire[^:]*:", "\r"),
        (r"Chiffrer le disque[^:]*:", "\r"),
        (r"Nom de la machine[^:]*:", "\r"),
        (r"pour continuer", "\r"),
        (r"Which one do you want to initialize[^\]]*\]", iface_answer),
        (r"Ip address for eth0[^\]]*\]", "dhcp\r"),
        (r"manual network configuration[^\]]*\]", "n\r"),
        (r"New password:", f"{PW}\r"),
        (r"Retype password:", f"{PW}\r"),
        (r"Which disk\(s\) would you like to use[^\]]*\]", "vda\r"),
        (r"How would you like to use it[^\]]*\]", "sys\r"),
        (r"Erase the above disk[^\]]*\]", "y\r"),
        (r"Enter ssh key[^\]]*\]", "none\r"),
        (r"Setup a user\?[^\]]*\]", "test\r"),
        (r"Full name for user[^\]]*\]", "\r"),
        (r"\? *(\([^)]*\) *)?\[[^\]\n]*\] *$", "\r"),   # toute autre question : valeur par défaut
    ], timeout=1200)
else:
    # installation 100 % automatique (fichier de réponses, aucune question)
    crypt = f"CHIFFREMENT=oui\\nPHRASE_CHIFFREMENT={PHRASE}\\n" if CHIFFRE else ""
    vm.run("printf 'DISQUE=/dev/vda\\nUTILISATEUR=test\\nMOT_DE_PASSE=%s\\nBUREAU=non\\nREDEMARRER=non\\n" + crypt + "' "
           f"'{PW}' > /tmp/auto.conf; cat /tmp/auto.conf")
    vm.run_interactive("sh /etc/plume/install.sh --auto /tmp/auto.conf || echo INSTALL-\"\"ECHEC", [
        (r"INSTALL-ECHEC", lambda m: die("install.sh --auto a échoué")),
        (r"ERROR: [^\r\n]*", lambda m: (step(f"apk : {m}"), None)[1]),
    ], timeout=1500)

step("Installation sur disque terminée")

# Uniquement pour le test : console série sur le système installé + sudo/doas sans mot de passe
if CHIFFRE:  # la racine est chiffrée : on l'ouvre pour la préparer
    vm.run(f"cryptsetup isLuks /dev/vda2 && echo 'RACINE CHIFFREE (LUKS)'; printf %s '{PHRASE}' | cryptsetup open /dev/vda2 tr --key-file - && echo 'PHRASE OK'")
vm.run(r"""for p in /dev/vda1 /dev/vda2 /dev/vda3 /dev/mapper/tr; do mount $p /mnt 2>/dev/null || continue; """
       r"""for f in /mnt/extlinux.conf /mnt/boot/extlinux.conf; do [ -f $f ] && { grep -q ttyS0 $f || sed -i -E 's/^([[:space:]]*APPEND .*)/\1 console=ttyS0,115200/' $f; }; done; """
       r"""[ -f /mnt/etc/update-extlinux.conf ] && sed -i 's/^default_kernel_opts="/&console=ttyS0,115200 /' /mnt/etc/update-extlinux.conf; """
       r"""[ -f /mnt/etc/inittab ] && { grep -q '^ttyS0' /mnt/etc/inittab || echo 'ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100' >> /mnt/etc/inittab; }; """
       r"""[ -d /mnt/etc/doas.d ] && echo 'permit nopass test' > /mnt/etc/doas.d/zz-test.conf; """
       r"""[ -d /mnt/etc/sudoers.d ] && { echo 'test ALL=(ALL) NOPASSWD: ALL' > /mnt/etc/sudoers.d/zz-test; chmod 0440 /mnt/etc/sudoers.d/zz-test; }; """
       r"""[ -d /mnt/etc/plume ] && echo "SUDO-SUR-DISQUE: $(ls /mnt/usr/bin/sudo 2>&1) $(ls -l /mnt/etc/sudoers.d/plume 2>&1)"; """
       r"""[ -d /mnt/etc/plume ] && echo "PLUME-SUR-DISQUE: $(ls /mnt/etc/plume | tr '\n' ' ')"; umount /mnt; done""")
if CHIFFRE:
    vm.run("cryptsetup close tr")
vm.send("poweroff\r")
vm.wait_eof()

# ===================== Phase 2 : système installé =====================
step("Phase 2 : démarrage sur le disque")
vm = VM(f"-drive file={DISK},if=virtio -nic user,model=virtio-net-pci")
if CHIFFRE:
    vm.expect(r"[Pp]assphrase", 900, "demande de la phrase secrète au démarrage")
    step("Phrase secrète demandée au démarrage")
    vm.send(PHRASE + "\r")
vm.login("test")
if CHIFFRE:
    vm.run("cat /proc/cmdline; ls /dev/mapper; df -h / | tail -1; sudo cryptsetup status root | head -4")
    step("SUCCES : disque chiffré, démarrage avec la phrase secrète")
    vm.send("sudo poweroff\r")
    vm.wait_eof()
    sys.exit(0)
step("Système installé (avant desktop.sh)")
vm.run('df -h / | tail -1; free -m | sed -n 2p; echo "paquets: $(apk info | wc -l)"')

step("sudo et apt (avant le bureau)")
vm.run('command -v sudo apt apt-get; sudo -n true && echo "SUDO OK"; '
       'sudo apt update | tail -1; sudo apt install -y tree build-essential 2>&1 | grep -E "→|OK:"; '
       'apt show tree | head -3; apt search fastfetch | head -2; sudo apt remove -y tree | tail -1; '
       'apt install tree; echo "code sans sudo: $?"', 900)

step("Lancement de desktop.sh (long en émulation)")
vm.run_interactive("sudo sh /etc/plume/desktop.sh", [
    (r"ERROR: [^\r\n]*", lambda m: (step(f"apk : {m}"), None)[1]),
], timeout=5400)
step("desktop.sh terminé, redémarrage")
vm.send("sudo reboot\r")
vm.login("test")

step("Vérifications après redémarrage")
vm.run("rc-status default | grep -E 'dbus|seatd|iwd|zram'; cat /proc/swaps; id")
vm.run('df -h / | tail -1; free -m | sed -n 2p; echo "paquets: $(apk info | wc -l)"')
vm.run('for p in sudo apt apt-get plume-logitheque plume-lanceur plume-menu plume-fonds plume-wifi plume-son plume-bluetooth plume-maj plume-parefeu plume-secureboot sway foot plume-term plume-terminal fuzzel swaylock firefox mpv imv zathura pcmanfm micro ibus-daemon; do '
       'command -v $p >/dev/null && echo "OK  $p" || echo "MANQUE $p"; done; '
       'ls /usr/share/ibus/component/; ls /usr/share/fonts')

step("Dépôt .apk Plume, AppArmor, Secure Boot")
vm.run("apk list -I 2>/dev/null | grep -E '^plume-' | cut -d' ' -f1 | tr '\\n' ' '; echo; grep plume /etc/apk/repositories; "
       "ls /usr/local/bin | grep -c plume- ; "
       "echo \"APPARMOR: $(cat /sys/module/apparmor/parameters/enabled 2>/dev/null) "
       "profils=$(sudo cat /sys/kernel/security/apparmor/profiles 2>/dev/null | wc -l)\"; cat /proc/cmdline; "
       "plume-secureboot", 180)

step("Sécurité : pare-feu, durcissement, mises à jour")
vm.run("rc-status | grep -E 'nftables|sysctl'; sudo nft list chain inet filter input | grep -E 'policy|established'; "
       "plume-parefeu; sudo plume-parefeu ouvrir 22 && sudo plume-parefeu fermer 22; "
       "sysctl kernel.kptr_restrict kernel.dmesg_restrict kernel.yama.ptrace_scope kernel.kexec_load_disabled "
       "net.ipv4.conf.all.rp_filter net.ipv4.tcp_syncookies; "
       "wget -q -O /dev/null https://dl-cdn.alpinelinux.org/alpine/ && echo 'INTERNET OK (pare-feu actif)'", 180)
vm.run("sudo -n /usr/bin/plume-maj --verifier && echo \"MAJ DISPO: $(cat /var/lib/plume/maj-dispo)\"; "
       "plume-maj --liste | tail -4", 600)

step("Écran de connexion (greetd + plume-hud)")
vm.run('command -v plume-hud && ls -l /usr/local/bin/plume-hud; '
       'for p in gcc make wayland-dev; do apk info -e $p >/dev/null && echo "RESTE $p" || echo "SUPPRIME $p"; done; '
       'rc-status default | grep greetd; sleep 20; ps -o user,args | grep -E "[c]age|[p]lume-hud"')
vm.run("sudo -u greetd sh -c 'export XDG_RUNTIME_DIR=/var/lib/greetd/runtime; "
       "export WAYLAND_DISPLAY=$(ls $XDG_RUNTIME_DIR | grep wayland- | grep -v lock | head -1); "
       "grim /tmp/greeter.png' && ls -l /tmp/greeter.png", 180)
vm.run("echo ===B64START greeter===; base64 /tmp/greeter.png; echo ===B64END===", 900)

step("Test de sway (sortie virtuelle, rendu logiciel)")
vm.run("export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1")
vm.run("(dbus-run-session sway > /tmp/sway.log 2>&1 &) ; sleep 40", 180)
vm.run('export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock | head -1); echo "socket: $SWAYSOCK"; '
       'export WAYLAND_DISPLAY=$(ls $XDG_RUNTIME_DIR | grep -m1 "^wayland-[0-9]*$"); echo "display: $WAYLAND_DISPLAY"; '
       'swaymsg -t get_version; swaymsg -t get_inputs | grep -m3 -E "xkb_layout_names|French|English"')
vm.run("swaymsg exec \"foot --hold sh -c '. /etc/os-release; echo \\$PRETTY_NAME — Plume; uname -r; "
       "echo; free -m; echo; df -h /; echo; echo 你好 Plume'\" ; sleep 30", 180)
vm.run('echo "fenetres foot: $(swaymsg -t get_tree | grep -c \'"app_id": "foot"\')"; '
       'echo "processus pipewire: $(ps | grep -c \'[p]ipewire\')"; '
       'echo "ibus-daemon: $(ps | grep -c \'[i]bus-daemon\')"; '
       'echo "plume-shell: $(ps | grep -c \'[p]lume-shell\')"; ls -l /usr/local/bin/plume-shell /usr/share/plume/wallpaper.png; free -m | sed -n 2p')
vm.run("grim /tmp/shot.png && ls -l /tmp/shot.png || tail -30 /tmp/sway.log", 180)
vm.run("echo ===B64START bureau===; base64 /tmp/shot.png; echo ===B64END===", 900)

step("Poste de pilotage (plume-term --hud)")
vm.run("plume-term --version; (ls -d ~/Documents ~/Bureau; plume-term --hud -e sh -c 'printf \"\\\\033[1;36mplume-term\\\\033[0m · 你好 · é\\\\n\"; ls --color=always /; exec sh' > /tmp/term.log 2>&1 &) ; sleep 20; "
       "echo \"fenetres plume-term: $(swaymsg -t get_tree | grep -c '\"app_id\": \"plume-term\"')\"; grim /tmp/term.png && ls -l /tmp/term.png; cat /tmp/term.log; pkill plume-term", 180)
vm.run("echo ===B64START terminal===; base64 /tmp/term.png; echo ===B64END===", 900)

step("Grille des applications (plume-lanceur)")
vm.run("(plume-menu > /tmp/menu.log 2>&1 &) ; sleep 15; grim /tmp/apps.png && ls -l /tmp/apps.png; cat /tmp/menu.log; "
       "pgrep -x plume-lanceur >/dev/null && echo 'LANCEUR OUVERT'; plume-menu; sleep 2; pgrep -x plume-lanceur >/dev/null || echo 'LANCEUR FERME'", 180)
vm.run("echo ===B64START applications===; base64 /tmp/apps.png; echo ===B64END===", 900)

step("Fond d'écran (plume-fonds)")
vm.run("ls /usr/local/share/plume/fonds; (plume-fonds > /tmp/fonds.log 2>&1 &) ; sleep 20; grim /tmp/fonds.png && ls -l /tmp/fonds.png; "
       "cat /tmp/fonds.log; pkill plume-fonds; mkdir -p ~/.config/plume; "
       "printf 'fond=/usr/local/share/plume/fonds/aurore.png\\nmode=fill\\n' > ~/.config/plume/fond.conf; "
       "plume-fonds --appliquer && echo 'FOND APPLIQUE'", 180)
vm.run("echo ===B64START fonds===; base64 /tmp/fonds.png; echo ===B64END===", 900)

step("Panneau wifi (plume-wifi)")
vm.run("(plume-wifi > /tmp/wifi.log 2>&1 &) ; sleep 12; grim /tmp/wifi.png && ls -l /tmp/wifi.png; "
       "cat /tmp/wifi.log; pgrep -x plume-wifi >/dev/null && echo 'WIFI OUVERT'; pkill plume-wifi", 120)
vm.run("echo ===B64START wifi===; base64 /tmp/wifi.png; echo ===B64END===", 900)

step("Logithèque Plume")
vm.run("ls -l /usr/local/bin/plume-logitheque /usr/local/share/plume/logitheque.txt; (plume-logitheque > /tmp/lg.log 2>&1 &) ; sleep 25; "
       "echo \"fenetres logitheque: $(swaymsg -t get_tree | grep -c '\"app_id\": \"plume-logitheque\"')\"; grim /tmp/lg.png && ls -l /tmp/lg.png; cat /tmp/lg.log; pkill plume-logitheque", 180)
vm.run("echo ===B64START logitheque===; base64 /tmp/lg.png; echo ===B64END===", 900)

step("Écran de verrouillage (plume-hud --lock)")
vm.run("(plume-hud --lock > /tmp/lock.log 2>&1 &) ; sleep 15; grim /tmp/lock.png && ls -l /tmp/lock.png; cat /tmp/lock.log", 180)
vm.run("echo ===B64START verrou===; base64 /tmp/lock.png; echo ===B64END===", 900)

step("slim-firmware.sh")
vm.run_interactive("sudo sh /etc/plume/slim-firmware.sh", [
    (r"Continuer \?", "o\r"),
], timeout=900)
vm.run('df -h / | tail -1; echo "paquets: $(apk info | wc -l)"')

step("SUCCES : tous les tests sont passés")
vm.send("sudo poweroff\r")
vm.wait_eof()
