#!/usr/bin/env python3
"""Recover the unsigned APK from a signed one.

An APK signed with scheme v2 or v3 is the unsigned APK with one insertion:
a signing block between the last local file entry and the central
directory, and an end-of-central-directory record whose offset was moved by
the length of that block. Nothing else is touched -- the entries, their
order, their compression and their timestamps are the bytes that were
signed, which is what makes the signature mean anything.

So the insertion can be undone exactly. Remove the block, put the offset
back, and the result is the file that was signed, byte for byte.

That is what makes a signature checkable against something other than
itself: a release signed with a key that never left its owner's machine can
be stripped by anyone, and the result compared with the unsigned APK that
carries a build provenance attestation. If the two match, the signed file
contains what the attested build produced, and the signature only says who
released it.

    apk-strip-signature.py signed.apk unsigned.apk

Scheme v1 -- a signature stored as ordinary entries under META-INF -- is not
reversible this way, and is refused rather than half-done.
"""

import sys

MAGIC = b"APK Sig Block 42"
EOCD_SIGNATURE = b"PK\x05\x06"
EOCD_MIN = 22


def find_eocd(data):
    """The offset of the end-of-central-directory record."""
    # It is last, and its comment may be up to 64 KiB, so the search starts
    # from the end. The last match is the record; an earlier one would be a
    # coincidence inside the archive.
    start = max(0, len(data) - (0xFFFF + EOCD_MIN))
    found = data.rfind(EOCD_SIGNATURE, start)
    if found < 0:
        raise SystemExit("not a zip: no end-of-central-directory record")
    return found


def central_directory_offset(data, eocd):
    return int.from_bytes(data[eocd + 16:eocd + 20], "little")


def has_v1_signature(data, eocd):
    """Whether the archive carries a JAR signature as entries."""
    directory = central_directory_offset(data, eocd)
    tail = data[directory:eocd]
    return b"META-INF/" in tail and (b".RSA" in tail or b".EC" in tail
                                     or b".DSA" in tail)


def strip(data):
    eocd = find_eocd(data)
    directory = central_directory_offset(data, eocd)
    if has_v1_signature(data, eocd):
        raise SystemExit(
            "this APK is signed with scheme v1, whose signature is stored as "
            "entries; removing those is not the inverse of adding them")
    if data[directory - 16:directory] != MAGIC:
        raise SystemExit("no APK signing block: this file is not signed with "
                         "scheme v2 or v3")
    # The block ends with its own length and the magic, and begins with the
    # same length, so its start follows from the end.
    size = int.from_bytes(data[directory - 24:directory - 16], "little")
    start = directory - (size + 8)
    if start < 0 or int.from_bytes(data[start:start + 8], "little") != size:
        raise SystemExit("the signing block does not agree with itself about "
                         "its length")
    stripped = bytearray(data[:start] + data[directory:])
    # The record now sits where the block used to begin.
    moved = find_eocd(stripped)
    stripped[moved + 16:moved + 20] = start.to_bytes(4, "little")
    return bytes(stripped)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    with open(argv[1], "rb") as signed:
        data = signed.read()
    with open(argv[2], "wb") as unsigned:
        unsigned.write(strip(data))


if __name__ == "__main__":
    main(sys.argv)
