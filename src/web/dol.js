// .dol to payload converter: a port of dol_to_payload() in tools/process_ipl.py (design D16), byte
// for byte, with the same error messages. Works as a classic script (window.PicoBootDol) and in Node.
(function (root) {
  "use strict";
  var EXPECTED_ENTRY = 0x81300000, EXPECTED_BASE = 0x01300000;

  // Python's 0x{:X} / 0x{:08X}
  function hex(n, width) {
    var s = n.toString(16).toUpperCase();
    while (s.length < (width || 0)) s = "0" + s;
    return "0x" + s;
  }

  // The bootrom's scrambling: three 16-bit LFSRs make a keystream, XORed into data in place.
  function scramble(data) {
    var t = 0x2953, u = 0xD9C2, v = 0x3FF1, x = 1;
    for (var it = 0; it < data.length; it++) {
      var acc = 0;
      for (var bit = 0; bit < 8; bit++) {
        var t0 = t & 1, t1 = (t >> 1) & 1, u0 = u & 1, u1 = (u >> 1) & 1, v0 = v & 1;
        x ^= t1 ^ v0;
        x ^= u0 | u1;
        x ^= (t0 ^ u1 ^ v0) & (t0 ^ u0);
        if (t0 === u0) { v >>= 1; if (v0) v ^= 0xB3D0; }
        if (t0 === 0) { u >>= 1; if (u0) u ^= 0xFB10; }
        t >>= 1;
        if (t0) t ^= 0xA740;
        acc = ((acc << 1) | x) & 255;
      }
      data[it] ^= acc;
    }
    return data;
  }

  // Lay the DOL's sections out in one image: returns [base address, entry point, image].
  function flattenDol(data) {
    if (data.length < 256)
      throw new Error("File is " + data.length + " bytes, smaller than a DOL header (256 bytes)");
    var view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    var w = function (i) { return view.getUint32(i * 4, false); };  // header word, big-endian
    var n, any = false;
    for (n = 0; n < 18; n++) if (w(36 + n)) any = true;
    if (!any) throw new Error("DOL contains no sections");
    for (n = 0; n < 18; n++) {
      var offset = w(n), address = w(18 + n), size = w(36 + n);
      if (size > 0 && address === 0)
        throw new Error("Section " + n + " has data but no load address");
      if (size > 0 && offset + size > data.length)
        throw new Error("Section " + n + " ends at " + hex(offset + size) + ", past the end of the file (" +
                        hex(data.length) + " bytes), file truncated?");
    }
    var min = Infinity, max = 0;      // sums reach 2^33, fine for doubles
    for (n = 0; n < 18; n++) {
      var a = w(18 + n), end = a + w(36 + n);
      if (a && a < min) min = a;
      if (end > max) max = end;
    }
    if (max - min > 0x01800000)
      throw new Error("DOL sections span " + hex(max - min) + " bytes, more than the 24 MB of GameCube main memory");
    var img = new Uint8Array(max - min);
    for (n = 0; n < 18; n++) {        // later sections win where they overlap
      var o = w(n), s = w(36 + n);
      if (s > 0) img.set(data.subarray(o, o + s), w(18 + n) - min);
    }
    return [min, w(56), img];
  }

  // The flash payload of a .dol: "IPLBOOT ", total size big-endian, zeros, the image scrambled from
  // offset 0x720, and a "PICO" footer.
  function dolToPayload(dol) {
    var f = flattenDol(dol);
    var base = (f[0] & 0x017FFFFF) >>> 0;
    var entry = ((f[1] & 0x017FFFFF) | 0x80000000) >>> 0;
    if (entry !== EXPECTED_ENTRY || base !== EXPECTED_BASE)
      throw new Error("Invalid entry point or base address: " + hex(entry, 8) + " / " + hex(base, 8) +
                      ", expected " + hex(EXPECTED_ENTRY, 8) + " / " + hex(EXPECTED_BASE, 8));
    var len = (f[2].length + 3) & ~3;                // whole words: the size field counts them
    var buf = new Uint8Array(0x720 + len);           // the keystream runs from 0, the output starts at 0x720
    buf.set(f[2], 0x720);
    scramble(buf);
    var size = 32 + len + 4, out = new Uint8Array(size);
    out.set([0x49, 0x50, 0x4C, 0x42, 0x4F, 0x4F, 0x54, 0x20]);  // "IPLBOOT "
    new DataView(out.buffer).setUint32(8, size, false);
    out.set(buf.subarray(0x720), 32);
    out.set([0x50, 0x49, 0x43, 0x4F], size - 4);                // "PICO"
    return out;
  }

  var api = { dolToPayload: dolToPayload, flattenDol: flattenDol, scramble: scramble };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.PicoBootDol = api;
})(typeof window !== "undefined" ? window : this);
