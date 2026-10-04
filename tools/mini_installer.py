#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Signing inside the browser installer, mini_installer.exe (sub-project D).

mini_installer.exe carries its payload as resources (packed_files.rc):
chrome.packed.7z (B7), the LZMA archive of chrome.7z, which holds the
browser's files; setup.ex_ (BL), setup.exe as a cabinet; and, in a component
build, setup.exe's DLLs, uncompressed (BD). sign() unpacks each, signs every
PE file in them, packs them again the way create_installer_archive.py does,
writes them back with UpdateResource and signs mini_installer.exe itself.
The input file is never changed. Windows only.
"""

from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
import sys
from ctypes import wintypes
from dataclasses import dataclass, replace
from pathlib import Path

import authenticode
from signing import SigningError

TYPES = ("B7", "BL", "BN", "BD")
_LOAD_AS_RESOURCES = 0x2 | 0x20  # LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE
_ERROR_RESOURCE_TYPE_NOT_FOUND = 1813
# create_installer_archive.py's CompressUsingLZMA (not its fast mode).
_PACKED_FLAGS = ["-m0=BCJ2", "-m1=LZMA:d27:fb128", "-m2=LZMA:d22:fb128:mf=bt2",
                 "-m3=LZMA:d22:fb128:mf=bt2", "-mb0:1", "-mb0s1:2", "-mb0s2:3", "-mtm-"]
_INNER_ARCHIVE = "chrome.7z"
# Windows' own: Git's shell puts coreutils' expand first on PATH. -r names the
# file as the cabinet stores it (setup.exe); without it, the file keeps the
# cabinet's name.
_EXPAND = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "expand.exe"


@dataclass(frozen=True)
class Resource:
    type: str
    name: str
    language: int
    data: bytes


def _kernel32():
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.LoadLibraryExW.restype = wintypes.HMODULE
    k.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    k.FreeLibrary.argtypes = [wintypes.HMODULE]
    k.FindResourceExW.restype = ctypes.c_void_p
    k.FindResourceExW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                  wintypes.WORD]
    k.SizeofResource.restype = wintypes.DWORD
    k.SizeofResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    k.LoadResource.restype = ctypes.c_void_p
    k.LoadResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    k.LockResource.restype = ctypes.c_void_p
    k.LockResource.argtypes = [ctypes.c_void_p]
    k.BeginUpdateResourceW.restype = wintypes.HANDLE
    k.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    k.UpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                  wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
    k.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    return k


def read_resources(exe: Path) -> list[Resource]:
    # WINFUNCTYPE exists only on Windows, so the module still imports elsewhere.
    name_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p,
                                   ctypes.c_void_p, ctypes.c_void_p)
    lang_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p,
                                   ctypes.c_void_p, wintypes.WORD, ctypes.c_void_p)
    k = _kernel32()
    k.EnumResourceNamesW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, name_proc,
                                     ctypes.c_void_p]
    k.EnumResourceLanguagesW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                         lang_proc, ctypes.c_void_p]
    module = k.LoadLibraryExW(str(exe), None, _LOAD_AS_RESOURCES)
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    out: list[Resource] = []
    try:
        for rtype in TYPES:
            names: list[str] = []
            integer_names: list[int] = []

            # An exception can't cross the callback, so it only collects.
            def on_name(_module, _type, name, _param):
                if (name or 0) >> 16 == 0:
                    integer_names.append(name or 0)
                else:
                    names.append(ctypes.wstring_at(name))
                return True

            if not k.EnumResourceNamesW(module, rtype, name_proc(on_name), None):
                if ctypes.get_last_error() != _ERROR_RESOURCE_TYPE_NOT_FOUND:
                    raise ctypes.WinError(ctypes.get_last_error())
            if integer_names:
                raise SigningError(f"{exe.name}: integer resource names in {rtype}")
            for name in names:
                languages: list[int] = []
                k.EnumResourceLanguagesW(module, rtype, name, lang_proc(
                    lambda _m, _t, _n, language, _p: languages.append(language) or True), None)
                for language in languages:
                    found = k.FindResourceExW(module, rtype, name, language)
                    size = k.SizeofResource(module, found)
                    pointer = k.LockResource(k.LoadResource(module, found))
                    out.append(Resource(rtype, name, language, ctypes.string_at(pointer, size)))
    finally:
        k.FreeLibrary(module)
    return out


def write_resources(exe: Path, resources: list[Resource]) -> None:
    k = _kernel32()
    handle = k.BeginUpdateResourceW(str(exe), False)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    for r in resources:
        buffer = ctypes.create_string_buffer(r.data, len(r.data))
        if not k.UpdateResourceW(handle, r.type, r.name, r.language, buffer, len(r.data)):
            error = ctypes.get_last_error()
            k.EndUpdateResourceW(handle, True)
            raise ctypes.WinError(error)
    if not k.EndUpdateResourceW(handle, False):
        raise ctypes.WinError(ctypes.get_last_error())


def _run(run, command: list, cwd: Path | None = None) -> None:
    out = run([str(c) for c in command], cwd=cwd, capture_output=True, text=True)
    if out.returncode:
        raise SigningError(f"{Path(str(command[0])).name} exited with {out.returncode}: "
                           f"{((out.stdout or '') + (out.stderr or '')).strip()[-300:]}")


def _unpack(r: Resource, d: Path, lzma: Path, run) -> Path:
    """Writes the resource's files under d/tree, ready to sign."""
    d.mkdir(parents=True)
    tree = d / "tree"
    source = d / r.name.lower()
    source.write_bytes(r.data)
    if r.type == "B7":
        _run(run, [lzma, "x", "-y", f"-o{d / 'inner'}", source])
        inner = d / "inner" / _INNER_ARCHIVE
        if not inner.exists():
            raise SigningError(f"{r.name} holds no {_INNER_ARCHIVE}")
        _run(run, [lzma, "x", "-y", f"-o{tree}", inner])
    elif r.type == "BL":
        tree.mkdir()
        _run(run, [_EXPAND, "-r", source, tree])
    else:
        tree.mkdir()
        shutil.move(source, tree / r.name.lower())
    return tree


def _repack(r: Resource, d: Path, lzma: Path, makecab: Path, run) -> bytes:
    tree, new = d / "tree", d / "new"
    new.mkdir()
    if r.type == "B7":
        inner = new / _INNER_ARCHIVE
        _run(run, [lzma, "a", "-t7z", inner, *sorted(p.name for p in tree.iterdir()), "-mx0"],
             cwd=tree)
        packed = new / r.name.lower()
        _run(run, [lzma, "a", "-t7z", *_PACKED_FLAGS, packed, inner])
        return packed.read_bytes()
    if r.type == "BL":
        files = list(tree.iterdir())
        if len(files) != 1:
            raise SigningError(f"{r.name} should hold one file, not {len(files)}")
        _run(run, [sys.executable, makecab, "/D", "CompressionType=LZX", "/D",
                   f"InputMtime={int(files[0].stat().st_mtime)}", "/V1", "/L", new, files[0]])
        return (new / (files[0].name[:-1] + "_")).read_bytes()
    return (tree / r.name.lower()).read_bytes()


def sign(exe: Path, out: Path, thumb: str, *, signtool: Path, lzma: Path, makecab: Path,
         work: Path, run=subprocess.run) -> list[Path]:
    """Writes a signed copy of `exe` to `out`; returns the PE files it signed inside."""
    resources = read_resources(exe)
    trees = [_unpack(r, work / f"{i}-{r.type}", lzma, run) for i, r in enumerate(resources)]
    inside = [p for tree in trees for p in authenticode.pe_files(tree)]
    if inside:
        authenticode.sign(inside, thumb, signtool, run)
    repacked = [replace(r, data=_repack(r, work / f"{i}-{r.type}", lzma, makecab, run))
                for i, r in enumerate(resources)]
    shutil.copyfile(exe, out)
    write_resources(out, repacked)
    authenticode.sign([out], thumb, signtool, run)
    return inside


def _extract_all(archive: Path, dest: Path, lzma: Path, run) -> None:
    _run(run, [lzma, "x", "-y", f"-o{dest}", archive])
    for inner in sorted(dest.rglob("*.7z")):
        _extract_all(inner, inner.with_name(inner.name + ".d"), lzma, run)


def unpacked_pe_files(exe: Path, work: Path, lzma: Path, run=subprocess.run) -> list[Path]:
    """Every PE file inside an installer's resources, archives within archives included."""
    files: list[Path] = []
    for i, r in enumerate(read_resources(exe)):
        d = work / f"{i}-{r.type}"
        d.mkdir(parents=True)
        source = d / r.name.lower()
        source.write_bytes(r.data)
        if r.type == "B7":
            _extract_all(source, d / "x", lzma, run)
            files += authenticode.pe_files(d / "x")
        elif r.type == "BL":
            (d / "x").mkdir()
            _run(run, [_EXPAND, "-r", source, d / "x"])
            files += authenticode.pe_files(d / "x")
        elif authenticode.is_pe(source):
            files.append(source)
    return files
