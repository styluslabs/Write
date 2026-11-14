#!/usr/bin/env python3
"""Extract the iPhoneOS SDK (plus version info) from an Xcode .xip without unpacking all of Xcode.

Usage: python3 extract_ios_sdk.py Xcode_27.2.xip [out.tar.gz]

Pure stdlib (works under Windows Python or WSL).  Streams the archive once:
  xar container -> "Content" member (pbzx: chunked xz) -> cpio (odc or newc) -> filter -> tar.gz
Output is a tarball so symlinks and file modes survive a Windows filesystem; unpack in the VM with
  tar xzf ios-sdk.tar.gz
"""

import io, lzma, os, re, struct, sys, tarfile, time, zlib
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor

# paths (relative to archive root, leading "./" stripped) to keep; prefix match
KEEP_PREFIXES = [
  "Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/",
  "Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Info.plist",
  "Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/version.plist",
  "Xcode.app/Contents/Info.plist",
  "Xcode.app/Contents/version.plist",
  # compiler-rt for iOS (needed for @available -> __isPlatformVersionAtLeast, etc.)
  "Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/",
]
# within the clang dir, only keep darwin runtime libs (skip headers for other platforms, etc.)
CLANG_DIR = KEEP_PREFIXES[-1]

def keep(path):
  if path.startswith(CLANG_DIR):
    return "/lib/darwin/" in path and "ios" in path.rsplit("/", 1)[-1]
  return any(path.startswith(p) for p in KEEP_PREFIXES)


PLACEHOLDER = b"NULLcanary"

# lld (21) can't parse the "arm64e.x1-ios" target in newer SDK .tbd stubs, and (unlike Apple's ld) won't link an arm64
#  binary against arm64e-only stubs, so drop arm64e.x1 and add arm64 to every target list containing arm64e
TBD_TARGETS = re.compile(rb"(targets:\s*\[)([^\]\n]*)(\])")

def fix_tbd_targets(m):
  targets = [t.strip() for t in m.group(2).split(b",") if t.strip()]
  targets = [t for t in targets if not t.startswith(b"arm64e.")]
  if b"arm64e-ios" in targets and b"arm64-ios" not in targets:
    targets.insert(0, b"arm64-ios")
  return m.group(1) + b" " + b", ".join(targets) + b" " + m.group(3)

def fix_tbd(data):
  return TBD_TARGETS.sub(fix_tbd_targets, data)

def log(msg):
  sys.stderr.write(msg + "\n")
  sys.stderr.flush()


# --- xar ---
def xar_member(f, name):
  hdr = f.read(28)
  magic, hsize, ver, toc_clen, toc_ulen, cksum = struct.unpack(">4sHHQQI", hdr)
  if magic != b"xar!":
    raise SystemExit("not a xar/xip file")
  f.seek(hsize)
  toc = ET.fromstring(zlib.decompress(f.read(toc_clen)))
  heap = hsize + toc_clen
  for fe in toc.iter("file"):
    if fe.findtext("name") == name:
      d = fe.find("data")
      enc = d.find("encoding").get("style", "")
      if "octet-stream" not in enc:
        raise SystemExit("unexpected xar encoding for %s: %s" % (name, enc))
      return heap + int(d.findtext("offset")), int(d.findtext("length"))
  raise SystemExit("member %r not found in xar" % name)


# --- pbzx: yields decompressed chunks in order, decompressing in parallel threads (lzma releases GIL) ---
def pbzx_chunks(f, length):
  end = f.tell() + length
  if f.read(4) != b"pbzx":
    raise SystemExit("Content is not pbzx")
  flags, = struct.unpack(">Q", f.read(8))
  def decode(chunk):
    if chunk[:6] == b"\xfd7zXZ\x00":
      return lzma.decompress(chunk, format=lzma.FORMAT_XZ)
    return chunk
  nworkers = max(2, os.cpu_count() or 2)
  pending = []
  with ThreadPoolExecutor(nworkers) as pool:
    while flags & (1 << 24) and f.tell() < end:
      flags, clen = struct.unpack(">QQ", f.read(16))
      pending.append(pool.submit(decode, f.read(clen)))
      if len(pending) > 2*nworkers:
        yield pending.pop(0).result()
    for p in pending:
      yield p.result()


class Stream:
  """File-like sequential reader over an iterator of byte chunks (enough for cpio + tarfile.addfile)."""
  def __init__(self, chunks):
    self.it = iter(chunks)
    self.buf = b""
    self.pos = 0
    self.total = 0
  def read(self, n):
    out = []
    while n > 0:
      if self.pos >= len(self.buf):
        try:
          self.buf = next(self.it)
        except StopIteration:
          break
        self.pos = 0
      piece = self.buf[self.pos:self.pos + n]
      self.pos += len(piece)
      n -= len(piece)
      out.append(piece)
    data = b"".join(out)
    self.total += len(data)
    return data
  def readexact(self, n):
    data = self.read(n)
    if len(data) != n:
      raise SystemExit("unexpected end of archive")
    return data
  def skip(self, n):
    while n > 0:
      n -= len(self.readexact(min(n, 1 << 20)))


class Limited:
  def __init__(self, s, n): self.s, self.n = s, n
  def read(self, k=-1):
    k = self.n if k < 0 else min(k, self.n)
    data = self.s.readexact(k)
    self.n -= len(data)
    return data


# --- cpio ---
def cpio_entries(s):
  """yields (name, mode, mtime, nlink, ino_key, size); caller must consume exactly size bytes after each."""
  while True:
    magic = s.readexact(6)
    if magic == b"070707":  # odc
      h = s.readexact(70)
      f = lambda a, b: int(h[a:b], 8)
      dev, ino, mode = f(0, 6), f(6, 12), f(12, 18)
      nlink, mtime, namesize, size = f(30, 36), f(42, 53), f(53, 59), f(59, 70)
      name = s.readexact(namesize)[:-1].decode("utf-8", "surrogateescape")
      pad = lambda n: 0
    elif magic in (b"070701", b"070702"):  # newc
      h = s.readexact(104)
      f = lambda i: int(h[8*i:8*i + 8], 16)
      ino, mode, mtime, nlink, size = f(0), f(1), f(5), f(4), f(6)
      dev = (f(7) << 32) | f(8)
      namesize = f(11)
      name = s.readexact(namesize)[:-1].decode("utf-8", "surrogateescape")
      s.skip((-(110 + namesize)) % 4)
      pad = lambda n: (-n) % 4
    else:
      raise SystemExit("bad cpio magic %r at offset %d" % (magic, s.total - 6))
    if name == "TRAILER!!!":
      return
    yield name, mode, mtime, nlink, (dev, ino), size
    s.skip(pad(size))


def list_entries(xip, out):
  """diagnostic: write TSV of every cpio entry: name, mode, nlink, dev:ino, size, first 16 bytes of data (hex)"""
  t0 = time.time()
  with open(xip, "rb") as f, open(out, "w", encoding="utf-8", errors="surrogateescape") as o:
    off, length = xar_member(f, "Content")
    f.seek(off)
    s = Stream(pbzx_chunks(f, length))
    o.write("name\tmode\tnlink\tdevino\tsize\thead\n")
    n, last_report = 0, 0
    for name, mode, mtime, nlink, ino_key, size in cpio_entries(s):
      head = s.readexact(min(size, 16))
      s.skip(size - len(head))
      o.write("%s\t%o\t%d\t%d:%d\t%d\t%s\n" % (name, mode, nlink, ino_key[0], ino_key[1], size, head.hex()))
      n += 1
      if s.total - last_report > (1 << 30):
        last_report = s.total
        log("  %.1f GB scanned, %d entries (%.0fs)" % (s.total/2**30, n, time.time() - t0))
  log("done: %d entries -> %s" % (n, out))


def main():
  if len(sys.argv) < 2:
    raise SystemExit(__doc__)
  if sys.argv[1] == "--list":
    return list_entries(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "xip-list.tsv")
  xip = sys.argv[1]
  out = sys.argv[2] if len(sys.argv) > 2 else "ios-sdk.tar.gz"
  t0 = time.time()
  with open(xip, "rb") as f, tarfile.open(out, "w:gz", compresslevel=6) as tar:
    off, length = xar_member(f, "Content")
    f.seek(off)
    s = Stream(pbzx_chunks(f, length))
    nkept, nfiles, sdk_seen, nbad = 0, 0, False, 0
    # Hardlinked files (e.g. headers shared by all SDKs): only the first entry in the archive has the real
    #  data; later entries w/ same ino have a placeholder ("NULLcanary" + zeros) of the same size.  So we
    #  cache (compressed) data of first occurrence of every hardlinked file (~200MB raw for Xcode 27)
    linkdata = {}  # ino_key -> zlib compressed data
    last_report = 0
    for name, mode, mtime, nlink, ino_key, size in cpio_entries(s):
      nfiles += 1
      path = name[2:] if name.startswith("./") else name
      if s.total - last_report > (1 << 30):
        last_report = s.total
        log("  %.1f GB scanned, %d entries, %d kept (%.0fs)" % (s.total/2**30, nfiles, nkept, time.time() - t0))
      wanted = path and path != "." and keep(path)
      data = None  # if set, entry's data has already been consumed from stream
      if nlink > 1 and mode & 0o170000 == 0o100000:
        if ino_key in linkdata:
          s.skip(size)
          data = zlib.decompress(linkdata[ino_key]) if wanted else b""
        else:
          data = s.readexact(size)
          if data.startswith(PLACEHOLDER):
            log("WARNING: placeholder data for first occurrence of %s" % path)
          linkdata[ino_key] = zlib.compress(data, 1)
      if not wanted:
        if data is None:
          s.skip(size)
        continue
      if "/SDKs/iPhoneOS" in path:
        sdk_seen = True
      ti = tarfile.TarInfo(path)
      ti.mode = mode & 0o7777
      ti.mtime = mtime
      ftype = mode & 0o170000
      if ftype == 0o040000:
        ti.type = tarfile.DIRTYPE
        s.skip(size)
        tar.addfile(ti)
      elif ftype == 0o120000:
        ti.type = tarfile.SYMTYPE
        ti.linkname = s.readexact(size).decode("utf-8", "surrogateescape")
        tar.addfile(ti)
      elif ftype == 0o100000:
        if path.endswith(".tbd"):
          if data is None:
            data = s.readexact(size)
          data = fix_tbd(data)
        if data is not None:
          if data.startswith(PLACEHOLDER):
            nbad += 1
          ti.size = len(data)
          tar.addfile(ti, io.BytesIO(data))
        else:
          ti.size = size
          tar.addfile(ti, Limited(s, size))
      else:
        s.skip(size)
        continue
      nkept += 1
  log("done: %d of %d entries kept -> %s (%.0fs)" % (nkept, nfiles, out, time.time() - t0))
  if nbad:
    log("ERROR: %d extracted files contain placeholder data" % nbad)
    sys.exit(1)
  if not sdk_seen:
    log("WARNING: no iPhoneOS SDK found in this xip - it may be a separately downloaded component")
    sys.exit(1)


if __name__ == "__main__":
  main()
