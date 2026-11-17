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
    conf = (f'DISQUE=/dev/vda\nUTILISATEUR=test\nMOT_DE_PASSE={PW}\nREDEMARRER=non\nNOYAU_EXTRA="console=ttyS0,115200"\n')
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
vm = VM(f"-no-reboot -drive file={DISK},if=virtio -nic user,model=virtio-net-pci")
if CHIFFRE:
    vm.expect(r"[Pp]assword|[Pp]assphrase", 900, "demande de la phrase secrète")
    vm.p.send(f"{PHRASE}\r")
    step("Phrase secrète acceptée")
vm.login("test", PW)
step("Connexion de l'utilisateur OK")
ok = True
ok &= vm.check("glibc", "ldd --version | head -1", r"GNU libc|GLIBC")
ok &= vm.check("11 composants Plume installés", "xbps-query -l | grep -c ' plume-'", r"\b1[01]\b")
for s in ("dbus", "seatd", "greetd", "nftables", "iwd", "bluetoothd", "zramen"):
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
# capture de l'écran de connexion (cage accepte grim)
vm.run(f"echo {PW} | sudo -S sh -c '"
       "XDG_RUNTIME_DIR=/var/lib/_greeter/runtime WAYLAND_DISPLAY=$(ls /var/lib/_greeter/runtime | grep -m1 ^wayland-[0-9]$) "
       "grim /tmp/g.png' ; echo ===B64START greeter=== ; base64 /tmp/g.png ; echo ===B64END===", 300)
vm.run("df -h / | tail -1; free -m | sed -n 2p")
step("SUCCES : tous les tests sont passés" if ok else "ECHEC : au moins un test a échoué")
vm.p.send(f"echo {PW} | sudo -S poweroff\r")
