#!/usr/bin/env python3
"""Test complet de Plume dans une VM QEMU, piloté par la console série.

Lancé par tests/vm-test.sh dans un conteneur Alpine (python3 + py3-pexpect + qemu).
1. démarre l'ISO live et lance /etc/plume/install.sh (réponses automatiques)
2. redémarre sur le disque, lance desktop.sh, redémarre
3. vérifie services, paquets, sway (capture d'écran), pinyin, puis slim-firmware.sh
"""
import re
import sys

import pexpect

ISO = sys.argv[1]
DISK = "/o/test/disk.qcow2"
PW = "plume-test-123"
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

iface = {"done": False}


def iface_answer(_):
    if iface["done"]:
        return "done\r"
    iface["done"] = True
    return "eth0\r"


vm.run_interactive("sh /etc/plume/install.sh", [
    (r"Disposition clavier[^:]*:", "\r"),
    (r"Fuseau horaire[^:]*:", "\r"),
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
step("Installation sur disque terminée")

# Uniquement pour le test : console série sur le système installé + doas sans mot de passe
vm.run(r"""for p in /dev/vda1 /dev/vda2 /dev/vda3; do mount $p /mnt 2>/dev/null || continue; """
       r"""for f in /mnt/extlinux.conf /mnt/boot/extlinux.conf; do [ -f $f ] && { grep -q ttyS0 $f || sed -i -E 's/^([[:space:]]*APPEND .*)/\1 console=ttyS0,115200/' $f; }; done; """
       r"""[ -f /mnt/etc/update-extlinux.conf ] && sed -i 's/^default_kernel_opts="/&console=ttyS0,115200 /' /mnt/etc/update-extlinux.conf; """
       r"""[ -f /mnt/etc/inittab ] && { grep -q '^ttyS0' /mnt/etc/inittab || echo 'ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100' >> /mnt/etc/inittab; }; """
       r"""[ -d /mnt/etc/doas.d ] && echo 'permit nopass test' > /mnt/etc/doas.d/zz-test.conf; """
       r"""[ -d /mnt/etc/plume ] && echo "PLUME-SUR-DISQUE: $(ls /mnt/etc/plume | tr '\n' ' ')"; umount /mnt; done""")
vm.send("poweroff\r")
vm.wait_eof()

# ===================== Phase 2 : système installé =====================
step("Phase 2 : démarrage sur le disque")
vm = VM(f"-drive file={DISK},if=virtio -nic user,model=virtio-net-pci")
vm.login("test")
step("Système installé (avant desktop.sh)")
vm.run('df -h / | tail -1; free -m | sed -n 2p; echo "paquets: $(apk info | wc -l)"')

step("Lancement de desktop.sh (long en émulation)")
vm.run_interactive("doas sh /etc/plume/desktop.sh", [
    (r"ERROR: [^\r\n]*", lambda m: (step(f"apk : {m}"), None)[1]),
], timeout=5400)
step("desktop.sh terminé, redémarrage")
vm.send("doas reboot\r")
vm.login("test")

step("Vérifications après redémarrage")
vm.run("rc-status default | grep -E 'dbus|seatd|iwd|zram'; cat /proc/swaps; id")
vm.run('df -h / | tail -1; free -m | sed -n 2p; echo "paquets: $(apk info | wc -l)"')
vm.run('for p in sway foot fuzzel swaylock firefox mpv imv zathura pcmanfm micro ibus-daemon; do '
       'command -v $p >/dev/null && echo "OK  $p" || echo "MANQUE $p"; done; '
       'ls /usr/share/ibus/component/; ls /usr/share/fonts')

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
       'echo "ibus-daemon: $(ps | grep -c \'[i]bus-daemon\')"; free -m | sed -n 2p')
vm.run("grim /tmp/shot.png && ls -l /tmp/shot.png || tail -30 /tmp/sway.log", 180)
vm.run("echo ===B64START===; base64 /tmp/shot.png; echo ===B64END===", 900)

step("slim-firmware.sh")
vm.run_interactive("doas sh /etc/plume/slim-firmware.sh", [
    (r"Continuer \?", "o\r"),
], timeout=900)
vm.run('df -h / | tail -1; echo "paquets: $(apk info | wc -l)"')

step("SUCCES : tous les tests sont passés")
vm.send("doas poweroff\r")
vm.wait_eof()
