# -*- coding: utf-8 -*-
"""Beach Buggy Racing PS Vita port by MeninoSung; patcher by WolffsRoom."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath

import bsdiff4


VERSION = "1.0"
LANGUAGES = [
    ("English", "EN"), ("Portugues (Brasil)", "PT"), ("Espanol", "ES"),
    ("Francais", "FR"), ("Portugues (Portugal)", "PTPT"), ("Italiano", "IT"),
    ("Russian", "RU"), ("Japanese", "JP"),
]

TR = {
    "EN": dict(instruction="Place exactly one compatible APK inside the APK folder.",
        output="The output will be created in VitaFiles\\beachbuggyracing.", found="APK found",
        noapk="No APK was found in the APK folder.", many="There is more than one APK in the APK folder. Leave only the correct file.",
        confirm="Start generating VitaFiles?", yes="Y", cancelled="Operation cancelled.", verify="Verifying APK...",
        bad="Incompatible APK. Use exactly Beach Buggy Racing 1.2.22", generating="Generating VitaFiles",
        checkfail="Failed to verify", success="SUCCESS! All files were generated and verified.",
        copy="Copy the 'beachbuggyracing' folder to ux0:data/ on the PS Vita.", error="ERROR", close="Press ENTER to close..."),
    "PT": dict(instruction="Coloque apenas um APK compativel dentro da pasta APK.",
        output="A saida sera criada em VitaFiles\\beachbuggyracing.", found="APK encontrado",
        noapk="Nenhum APK encontrado na pasta APK.", many="Ha mais de um APK na pasta APK. Deixe apenas o arquivo correto.",
        confirm="Iniciar a geracao do VitaFiles?", yes="S", cancelled="Operacao cancelada.", verify="Verificando o APK...",
        bad="APK incompativel. Use exatamente o Beach Buggy Racing 1.2.22", generating="Gerando VitaFiles",
        checkfail="Falha ao verificar", success="SUCESSO! Todos os arquivos foram gerados e verificados.",
        copy="Copie a pasta 'beachbuggyracing' para ux0:data/ no PS Vita.", error="ERRO", close="Pressione ENTER para fechar..."),
    "ES": dict(instruction="Coloca un solo APK compatible dentro de la carpeta APK.",
        output="La salida se creara en VitaFiles\\beachbuggyracing.", found="APK encontrado",
        noapk="No se encontro ningun APK en la carpeta APK.", many="Hay mas de un APK en la carpeta APK. Deja solo el archivo correcto.",
        confirm="Iniciar la generacion de VitaFiles?", yes="S", cancelled="Operacion cancelada.", verify="Verificando el APK...",
        bad="APK incompatible. Usa exactamente Beach Buggy Racing 1.2.22", generating="Generando VitaFiles",
        checkfail="Error al verificar", success="EXITO! Todos los archivos fueron generados y verificados.",
        copy="Copia la carpeta 'beachbuggyracing' a ux0:data/ en la PS Vita.", error="ERROR", close="Pulsa ENTER para cerrar..."),
    "FR": dict(instruction="Placez un seul APK compatible dans le dossier APK.",
        output="La sortie sera creee dans VitaFiles\\beachbuggyracing.", found="APK trouve",
        noapk="Aucun APK trouve dans le dossier APK.", many="Plusieurs APK sont presents. Gardez uniquement le bon fichier.",
        confirm="Commencer la creation de VitaFiles ?", yes="O", cancelled="Operation annulee.", verify="Verification de l'APK...",
        bad="APK incompatible. Utilisez exactement Beach Buggy Racing 1.2.22", generating="Creation de VitaFiles",
        checkfail="Echec de verification", success="SUCCES ! Tous les fichiers ont ete crees et verifies.",
        copy="Copiez le dossier 'beachbuggyracing' vers ux0:data/ sur la PS Vita.", error="ERREUR", close="Appuyez sur ENTREE pour fermer..."),
    "PTPT": dict(instruction="Coloque apenas um APK compativel dentro da pasta APK.",
        output="A saida sera criada em VitaFiles\\beachbuggyracing.", found="APK encontrado",
        noapk="Nenhum APK encontrado na pasta APK.", many="Existe mais de um APK. Deixe apenas o ficheiro correto.",
        confirm="Iniciar a criacao do VitaFiles?", yes="S", cancelled="Operacao cancelada.", verify="A verificar o APK...",
        bad="APK incompativel. Utilize exatamente Beach Buggy Racing 1.2.22", generating="A criar VitaFiles",
        checkfail="Falha na verificacao", success="SUCESSO! Todos os ficheiros foram criados e verificados.",
        copy="Copie a pasta 'beachbuggyracing' para ux0:data/ na PS Vita.", error="ERRO", close="Prima ENTER para fechar..."),
    "IT": dict(instruction="Inserisci un solo APK compatibile nella cartella APK.",
        output="I file verranno creati in VitaFiles\\beachbuggyracing.", found="APK trovato",
        noapk="Nessun APK trovato nella cartella APK.", many="Ci sono piu APK. Lascia solo il file corretto.",
        confirm="Avviare la generazione di VitaFiles?", yes="S", cancelled="Operazione annullata.", verify="Verifica dell'APK...",
        bad="APK incompatibile. Usa esattamente Beach Buggy Racing 1.2.22", generating="Generazione VitaFiles",
        checkfail="Verifica fallita", success="SUCCESSO! Tutti i file sono stati generati e verificati.",
        copy="Copia la cartella 'beachbuggyracing' in ux0:data/ sulla PS Vita.", error="ERRORE", close="Premi INVIO per chiudere..."),
    "RU": dict(instruction="Pomestite odin sovmestimiy APK v papku APK.", output="Rezultat budet sozdan v VitaFiles\\beachbuggyracing.",
        found="APK nayden", noapk="APK ne nayden v papke APK.", many="V papke neskolko APK. Ostavte tolko pravilniy fayl.",
        confirm="Nachat sozdanie VitaFiles?", yes="Y", cancelled="Operatsiya otmenena.", verify="Proverka APK...",
        bad="Nesovmestimiy APK. Ispolzuyte Beach Buggy Racing 1.2.22", generating="Sozdanie VitaFiles",
        checkfail="Oshibka proverki", success="USPEH! Vse fayli sozdani i provereni.",
        copy="Skopiruyte papku 'beachbuggyracing' v ux0:data/ na PS Vita.", error="OSHIBKA", close="Nazhmite ENTER dlya vihoda..."),
    "JP": dict(instruction="APK foruda ni taiou APK wo hitotsu dake oite kudasai.", output="VitaFiles\\beachbuggyracing ni sakusei shimasu.",
        found="APK mitsukarimashita", noapk="APK foruda ni APK ga arimasen.", many="APK ga futatsu ijou arimasu. Tadashii fairu dake nokoshite kudasai.",
        confirm="VitaFiles no sakusei wo hajimemasu ka?", yes="Y", cancelled="Kyanseru shimashita.", verify="APK kakunin-chu...",
        bad="Taiou shiteinai APK desu. Beach Buggy Racing 1.2.22 wo tsukatte kudasai", generating="VitaFiles sakusei-chu",
        checkfail="Kakunin ni shippai", success="SEIKOU! Subete no fairu wo sakusei kakunin shimashita.",
        copy="'beachbuggyracing' foruda wo PS Vita no ux0:data/ ni kopi shite kudasai.", error="ERAA", close="ENTER de shuuryou..."),
}


def app_dir() -> Path:
    return Path(sys.executable if getattr(sys, "frozen", False) else __file__).resolve().parent


def resource(relative: str) -> Path:
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    return root / relative


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def safe_member(name: str, text: dict) -> str:
    value = PurePosixPath(name)
    if value.is_absolute() or ".." in value.parts:
        raise ValueError(text["checkfail"])
    return value.as_posix()


def find_apk(text: dict) -> Path:
    folder = app_dir() / "APK"
    matches = sorted(folder.glob("*.apk")) if folder.is_dir() else []
    if not matches:
        raise ValueError(text["noapk"])
    if len(matches) > 1:
        raise ValueError(text["many"])
    return matches[0]


def generate(apk: Path, manifest: dict, text: dict) -> Path:
    destination = app_dir() / "VitaFiles" / manifest["output_folder"]
    temporary = destination.with_name(destination.name + ".tmp")
    if temporary.exists():
        shutil.rmtree(temporary)
    temporary.mkdir(parents=True)
    files = manifest["files"]
    try:
        with zipfile.ZipFile(apk) as archive:
            names = set(archive.namelist())
            for index, record in enumerate(files, 1):
                relative = safe_member(record["output"], text)
                source = record.get("source")
                original = archive.read(source) if source and source in names else b""
                output = temporary / Path(*PurePosixPath(relative).parts)
                output.parent.mkdir(parents=True, exist_ok=True)
                if record["mode"] == "copy":
                    output.write_bytes(original)
                else:
                    patch = resource("patch_data/patches") / record["patch"]
                    old = temporary / ".source.tmp"
                    old.write_bytes(original)
                    bsdiff4.file_patch(str(old), str(output), str(patch))
                    old.unlink(missing_ok=True)
                if output.stat().st_size != record["size"] or sha256(output) != record["sha256"]:
                    raise ValueError(f"{text['checkfail']}: {relative}")
                percent = index * 100 // len(files)
                print(f"\r  {text['generating']}... {percent:3d}%", end="", flush=True)
        print()
        if destination.exists():
            shutil.rmtree(destination)
        os.replace(temporary, destination)
        return destination
    except Exception:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def choose_language() -> str:
    while True:
        print("  SELECT LANGUAGE / SELECIONE O IDIOMA")
        print()
        for index, (name, _) in enumerate(LANGUAGES, 1):
            print(f"    [{index}] {name}")
        print("    [0] Exit / Sair")
        print()
        choice = input("  > ").strip()
        if choice == "0":
            return ""
        if choice.isdigit() and 1 <= int(choice) <= len(LANGUAGES):
            return LANGUAGES[int(choice) - 1][1]
        print("\n  Invalid option / Opcao invalida.\n")


def draw_header() -> None:
    print("=" * 66)
    print("          BEACH BUGGY RACING - PS VITA PORT")
    print(f"                       PATCHER v{VERSION}")
    print("                    Port by MeninoSung")
    print("                   Patcher by WolffsRoom")
    print("=" * 66)
    print()


def main() -> int:
    os.system("title Beach Buggy Racing - PS Vita Patcher")
    os.system("color 0A")
    draw_header()
    lang = choose_language()
    if not lang:
        return 0
    text = TR[lang]
    os.system("cls")
    draw_header()
    print("-" * 66)
    print(f"  {text['instruction']}")
    print(f"  {text['output']}")
    print("-" * 66 + "\n")
    try:
        manifest = json.loads(resource("patch_data/manifest.json").read_text(encoding="utf-8"))
        apk = find_apk(text)
        print(f"  {text['found']}: {apk.name}")
        print(f"  {text['confirm']} [{text['yes']}/N]")
        answer = input("  > ").strip().upper()
        if answer != text["yes"]:
            print(f"\n  {text['cancelled']}")
            print()
            input(f"  {text['close']}")
            return 0
        print(f"\n  {text['verify']}", end="", flush=True)
        expected = manifest["apk_sha256"].lower()
        if apk.stat().st_size != manifest["apk_size"] or sha256(apk) != expected:
            raise ValueError(
                f"{text['bad']} "
                f"(SHA-256 {expected[:12].upper()}...)."
            )
        print(" OK")
        result = generate(apk, manifest, text)
        print()
        print(f"  {text['success']}")
        print(f"  {result}")
        print()
        print(f"  {text['copy']}")
        code = 0
    except Exception as exc:
        print()
        print(f"  {text['error']}: {exc}")
        code = 1
    print()
    input(f"  {text['close']}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
