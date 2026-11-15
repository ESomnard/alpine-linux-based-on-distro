"""Faux greetd pour tester le greeter : mot de passe attendu « plume123 »."""
import json, os, socket, struct, sys

path = sys.argv[1]
if os.path.exists(path):
    os.unlink(path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path)
os.chmod(path, 0o777)
srv.listen(5)
log = open("/tmp/greetd.log", "a", buffering=1)

def recv(c):
    h = c.recv(4)
    if len(h) < 4:
        return None
    n = struct.unpack("=I", h)[0]
    b = b""
    while len(b) < n:
        b += c.recv(n - len(b))
    return json.loads(b)

def send(c, obj):
    d = json.dumps(obj).encode()
    c.sendall(struct.pack("=I", len(d)) + d)

while True:
    c, _ = srv.accept()
    while True:
        m = recv(c)
        if m is None:
            break
        shown = dict(m)
        if "response" in shown:
            shown["response"] = "***"
        log.write(json.dumps(shown) + "\n")
        t = m["type"]
        if t == "create_session":
            send(c, {"type": "auth_message", "auth_message_type": "secret", "auth_message": "Password: "})
        elif t == "post_auth_message_response":
            if m.get("response") == "plume123":
                send(c, {"type": "success"})
            else:
                send(c, {"type": "error", "error_type": "auth_error", "description": "pam_authenticate failed"})
        elif t == "start_session":
            # comme greetd 0.10 : /bin/sh -c "...; exec <cmd joint par des espaces>"
            line = "exec " + " ".join(m["cmd"])
            log.write("SESSION DEMARREE: %s\n" % line)
            import subprocess
            check = subprocess.run(["/bin/sh", "-n", "-c", line])
            log.write("syntaxe shell: %s\n" % ("OK" if check.returncode == 0 else "ERREUR"))
            first = " ".join(m["cmd"]).split()[0]
            log.write("commande '%s' %s\n" % (first, "trouvée" if subprocess.run(["sh","-c","command -v " + first], capture_output=True).returncode == 0 else "INTROUVABLE"))
            send(c, {"type": "success"})
        elif t == "cancel_session":
            send(c, {"type": "success"})
    c.close()
