#!/usr/bin/env python3
"""Generate and sign TinyHand Windows update metadata with OpenSSL."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def public_blob(private_key: Path) -> bytes:
    der = run(["openssl", "pkey", "-in", str(private_key), "-pubout", "-outform", "DER"],
              capture_output=True).stdout
    assert len(der) >= 65 and der[-65] == 4, "expected a P-256 public key"
    return struct.pack("<II", 0x31534345, 32) + der[-64:]


def keygen(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    private = directory / "update-private-key.pem"
    if private.exists():
        raise SystemExit(f"refusing to overwrite {private}")
    run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(private)])
    blob = public_blob(private)
    (directory / "update-public-key.blob").write_bytes(blob)
    values = ", ".join(f"0x{x:02x}" for x in blob)
    header = ("#pragma once\n#include <cstdint>\n#include <vector>\n\n"
              "// ECDSA P-256 public key for this fork's signed Windows update feed.\n"
              "inline std::vector<uint8_t> update_public_key() {\n"
              f"    static const uint8_t key[] = {{{values}}};\n"
              "    return std::vector<uint8_t>(std::begin(key), std::end(key));\n}\n")
    (directory / "update_key.h").write_text(header)
    print(f"Created private key, public blob, and update_key.h in {directory}")


def sign(private_key: Path, version: str, package: Path, output: Path) -> None:
    data = package.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    tag = f"windows-v{version}"
    url = f"https://github.com/Arisaya1/tinyhand/releases/download/{tag}/{package.name}"
    payload = f"{version}\n{url}\n{len(data)}\n{digest}\n".encode()
    work = output.parent
    work.mkdir(parents=True, exist_ok=True)
    payload_path = work / ".update-payload.txt"
    signature_path = work / ".update-signature.der"
    public_path = work / ".update-public.pem"
    payload_path.write_bytes(payload)
    run(["openssl", "pkey", "-in", str(private_key), "-pubout", "-out", str(public_path)])
    run(["openssl", "dgst", "-sha256", "-sign", str(private_key), "-out", str(signature_path), str(payload_path)])
    verified = run(["openssl", "dgst", "-sha256", "-verify", str(public_path), "-signature",
                    str(signature_path), str(payload_path)], capture_output=True, text=True)
    signature = base64.b64encode(signature_path.read_bytes()).decode()
    manifest = {"version": version, "url": url, "size": len(data), "sha256": digest, "signature": signature}
    output.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    payload_path.unlink()
    signature_path.unlink()
    public_path.unlink()
    print(f"OpenSSL: {verified.stdout.strip()}")
    print(f"Signed {package.name}: {len(data)} bytes, SHA-256 {digest}")
    print(f"Wrote {output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    generate = sub.add_parser("keygen")
    generate.add_argument("directory", type=Path)
    signer = sub.add_parser("sign")
    signer.add_argument("private_key", type=Path)
    signer.add_argument("version")
    signer.add_argument("package", type=Path)
    signer.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.command == "keygen":
        keygen(args.directory)
    else:
        sign(args.private_key, args.version, args.package, args.output)


if __name__ == "__main__":
    main()
