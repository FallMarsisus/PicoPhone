import subprocess

# On n'importe pas "Import", on l'utilise directement car PlatformIO l'injecte
try:
    Import("env")
except NameError:
    # Si on lance le script hors de PlatformIO, on définit un env vide pour éviter les erreurs
    env = {}

def get_git_commit_hash():
    try:
        # Récupère le hash court (7 caractères)
        # Sur Mac, assure-toi que 'git' est dans ton PATH
        return subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('ascii').strip()
    except Exception:
        return "unknown"

commit_hash = get_git_commit_hash()

# Ajout de la macro pour le compilateur
# La syntaxe est un peu spécifique pour bien échapper les guillemets en C++
try:
    env.Append(CPPDEFINES=[
        ("COMMIT_HASH", f'\\"{commit_hash}\\"')
    ])
    print(f"--- Git Commit Hash ajouté : {commit_hash} ---")
except AttributeError:
    # Si lancé hors PlatformIO
    print(f"Commit Hash détecté : {commit_hash}")