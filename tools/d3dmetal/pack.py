"""Pack the minimum D3DMetal runtime (D3D11 only) from Apple's Game Porting Toolkit redist.

    python -I pack.py <extracted-redist-dir> <out.tar.xz>

Layout: d3dmetal-4.0b3/external/{D3DMetal.framework/Versions/A/..., libd3dshared.dylib},
d3dmetal-4.0b3/x86_64-windows/{d3d11.dll, dxgi.dll}. engine/assemble.sh recreates the framework's
symlinks and the x86_64-unix/*.so links to libd3dshared.dylib. Deterministic (mtime 0, sorted).
"""
import hashlib
import io
import lzma
import os
import sys
import tarfile

src, out = sys.argv[1], sys.argv[2]
top = "d3dmetal-4.0b3"
files = []
for root, _, names in os.walk(os.path.join(src, "external", "D3DMetal.framework", "Versions", "A")):
    for n in names:
        files.append(os.path.relpath(os.path.join(root, n), src).replace(os.sep, "/"))
files += ["external/libd3dshared.dylib", "wine/x86_64-windows/d3d11.dll", "wine/x86_64-windows/dxgi.dll"]
files.sort()

buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as tar:
    for f in files:
        arc = top + "/" + (f[len("wine/"):] if f.startswith("wine/") else f)
        path = os.path.join(src, f)
        ti = tarfile.TarInfo(arc)
        ti.size = os.path.getsize(path)
        ti.mtime = 0
        ti.uid = ti.gid = 0
        ti.uname = ti.gname = ""
        ti.mode = 0o755 if arc.endswith((".dylib", ".dll", "/D3DMetal")) else 0o644
        with open(path, "rb") as fh:
            tar.addfile(ti, fh)
data = lzma.compress(buf.getvalue(), preset=9 | lzma.PRESET_EXTREME)
with open(out, "wb") as fh:
    fh.write(data)
digest = hashlib.sha256(data).hexdigest()
with open(out + ".sha256", "w") as fh:
    fh.write(f"{digest}  {os.path.basename(out)}\n")
print(len(files), "files,", len(buf.getvalue()) // 1048576, "MB tar ->", len(data) // 1048576, "MB xz,", digest)
