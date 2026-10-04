#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Keys in this PC's TPM, through Windows' Microsoft Platform Crypto Provider.

A key created here is generated inside the TPM and can't be exported: its
private half never exists in this process or on disk. NCrypt signs a SHA-256
digest and returns r | s. With pin_protected, Windows asks for a PIN when the
key is created, then each time a process opens it to sign; open_signer opens
it for each signature. Windows only.
"""

from __future__ import annotations

import ctypes
import sys
from ctypes import wintypes

import ecdsa_p256
import signing

PROVIDER = "Microsoft Platform Crypto Provider"
NTE_BAD_KEYSET = 0x80090016
_ECDSA_P256 = "ECDSA_P256"
_ECC_PUBLIC_BLOB = "ECCPUBLICBLOB"
_ECS1_MAGIC = 0x31534345  # BCRYPT_ECDSA_PUBLIC_P256_MAGIC
_UI_POLICY = "UI Policy"
_UI_PROTECT_KEY_FLAG = 0x1
_HANDLE = ctypes.c_size_t  # NCRYPT_HANDLE is a ULONG_PTR


class TpmError(signing.SigningError):
    def __init__(self, call: str, status: int):
        self.status = status & 0xFFFFFFFF
        super().__init__(f"{call} failed with 0x{self.status:08x}")


class _UiPolicy(ctypes.Structure):
    _fields_ = [("dwVersion", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("pszCreationTitle", wintypes.LPCWSTR), ("pszFriendlyName", wintypes.LPCWSTR),
                ("pszDescription", wintypes.LPCWSTR)]


def public_der_from_blob(blob: bytes) -> bytes:
    """A DER SubjectPublicKeyInfo from a BCRYPT_ECCKEY_BLOB of a P-256 key."""
    magic = int.from_bytes(blob[0:4], "little")
    size = int.from_bytes(blob[4:8], "little")
    if magic != _ECS1_MAGIC or size != 32 or len(blob) != 72:
        raise ValueError("not a P-256 ECDSA public key blob")
    return ecdsa_p256.spki((int.from_bytes(blob[8:40], "big"),
                            int.from_bytes(blob[40:72], "big")))


def _library():
    lib = ctypes.WinDLL("ncrypt")
    handle_p = ctypes.POINTER(_HANDLE)
    dword_p = ctypes.POINTER(wintypes.DWORD)
    signatures = {
        "NCryptOpenStorageProvider": [handle_p, wintypes.LPCWSTR, wintypes.DWORD],
        "NCryptCreatePersistedKey": [_HANDLE, handle_p, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                     wintypes.DWORD, wintypes.DWORD],
        "NCryptSetProperty": [_HANDLE, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD,
                              wintypes.DWORD],
        "NCryptFinalizeKey": [_HANDLE, wintypes.DWORD],
        "NCryptOpenKey": [_HANDLE, handle_p, wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD],
        "NCryptExportKey": [_HANDLE, _HANDLE, wintypes.LPCWSTR, ctypes.c_void_p,
                            ctypes.c_void_p, wintypes.DWORD, dword_p, wintypes.DWORD],
        "NCryptSignHash": [_HANDLE, ctypes.c_void_p, ctypes.c_char_p, wintypes.DWORD,
                           ctypes.c_void_p, wintypes.DWORD, dword_p, wintypes.DWORD],
        "NCryptDeleteKey": [_HANDLE, wintypes.DWORD],
        "NCryptFreeObject": [_HANDLE],
    }
    for name, argtypes in signatures.items():
        function = getattr(lib, name)
        function.argtypes = argtypes
        function.restype = ctypes.c_long
    return lib


class _Provider:
    """The storage provider, opened for one operation."""

    def __enter__(self) -> _Provider:
        if sys.platform != "win32":
            raise signing.SigningError("keys in the TPM need Windows")
        self.lib = _library()
        self.handle = _HANDLE()
        self.check("NCryptOpenStorageProvider",
                   self.lib.NCryptOpenStorageProvider(ctypes.byref(self.handle), PROVIDER, 0))
        return self

    def __exit__(self, *exc) -> None:
        self.lib.NCryptFreeObject(self.handle)

    @staticmethod
    def check(call: str, status: int) -> None:
        if status:
            raise TpmError(call, status)

    def open(self, name: str) -> _HANDLE:
        key = _HANDLE()
        self.check("NCryptOpenKey",
                   self.lib.NCryptOpenKey(self.handle, ctypes.byref(key), name, 0, 0))
        return key

    def public_der(self, key: _HANDLE) -> bytes:
        size = wintypes.DWORD()
        self.check("NCryptExportKey", self.lib.NCryptExportKey(
            key, 0, _ECC_PUBLIC_BLOB, None, None, 0, ctypes.byref(size), 0))
        buffer = ctypes.create_string_buffer(size.value)
        self.check("NCryptExportKey", self.lib.NCryptExportKey(
            key, 0, _ECC_PUBLIC_BLOB, None, buffer, size, ctypes.byref(size), 0))
        return public_der_from_blob(buffer.raw[:size.value])


def create_key(name: str, pin_protected: bool) -> bytes:
    """Creates the key in the TPM; returns its public key. Fails if it exists."""
    with _Provider() as provider:
        key = _HANDLE()
        provider.check("NCryptCreatePersistedKey", provider.lib.NCryptCreatePersistedKey(
            provider.handle, ctypes.byref(key), _ECDSA_P256, name, 0, 0))
        try:
            if pin_protected:
                policy = _UiPolicy(1, _UI_PROTECT_KEY_FLAG, None, name,
                                   "Signs Project Ghost's update packages.")
                provider.check("NCryptSetProperty", provider.lib.NCryptSetProperty(
                    key, _UI_POLICY, ctypes.byref(policy), ctypes.sizeof(policy), 0))
            provider.check("NCryptFinalizeKey", provider.lib.NCryptFinalizeKey(key, 0))
            return provider.public_der(key)
        finally:
            provider.lib.NCryptFreeObject(key)


def key_exists(name: str) -> bool:
    with _Provider() as provider:
        try:
            key = provider.open(name)
        except TpmError as e:
            if e.status == NTE_BAD_KEYSET:
                return False
            raise
        provider.lib.NCryptFreeObject(key)
        return True


def delete_key(name: str) -> None:
    with _Provider() as provider:
        key = provider.open(name)
        status = provider.lib.NCryptDeleteKey(key, 0)  # frees the handle when it succeeds
        if status:
            provider.lib.NCryptFreeObject(key)
            raise TpmError("NCryptDeleteKey", status)


def open_signer(name: str) -> signing.Signer:
    with _Provider() as provider:
        key = provider.open(name)
        try:
            public = provider.public_der(key)
        finally:
            provider.lib.NCryptFreeObject(key)

    def sign_digest(digest: bytes) -> bytes:
        if len(digest) != 32:
            raise ValueError("a SHA-256 digest is 32 bytes")
        with _Provider() as provider:
            key = provider.open(name)
            try:
                size = wintypes.DWORD()
                signature = ctypes.create_string_buffer(64)
                provider.check("NCryptSignHash", provider.lib.NCryptSignHash(
                    key, None, digest, 32, signature, 64, ctypes.byref(size), 0))
                return ecdsa_p256.der_from_raw(signature.raw[:size.value])
            finally:
                provider.lib.NCryptFreeObject(key)

    return signing.Signer(name, public, sign_digest)
