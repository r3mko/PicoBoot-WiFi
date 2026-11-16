// .dol to payload converter: a port of dol_to_payload() in tools/process_ipl.py (design D16), byte
// for byte, with the same error messages. Works as a classic script (window.PicoBootDol) and in Node.
(function (root) {
  "use strict";
  var EXPECTED_ENTRY = 0x81300000, EXPECTED_BASE = 0x01300000, LOW_END = 0x80003100;
  // The D43 loader stub: the bytes of src/dolstub/dolstub.bin (built from dolstub.S), 576 bytes.
  var STUB_HEX =
    "386000007c7b03a63c600130386300187c7a03a64c000064480000057fe802a63c0080006000000c7c1a43a63c600c00" +
    "380000007c15fba67c12fba63880601c7c041a6c7000000141820014388060007c041a6c600000017c041b6c38805008" +
    "380008047c041b6c38a050347c051a6c5400045e7c051b6c7c041a6c5400056d4082fff83880000038000c407c0903a6" +
    "38a0fffc948500044200fffc3c80480038a001009085000038a50100280517004081fff43c804c006084006490800c00" +
    "388040287c041a6c540096fa38bf02007c05002e90000028388060247c041a6c38c000012c0000ff4182000c3cc01000" +
    "60c600043880302c7c041a6c5400273e7cc6021490c0002c3c0009a76000ec80900000f83c001cf76000c580900000fc" +
    "7cad42e67ccc42e67ced42e67c0538004082fff020c600007ca5019090a030d890c030dc3900000039203100480000ad" +
    "3bdf0224839e0000811e0004813e0008550800be5520f0bf418200187c0903a638a8fffc38800000948500044200fffc" +
    "48000079837e000c3b5e00102c1b000041820048811a000080ba0004813a0008550800be7ca5f2145520f0bf4182001c" +
    "7c0903a638a5fffc38c8fffc84850004948600044200fff8480000313b5a000c3b7bffff4bffffb87c0004ac7c10faa6" +
    "600008007c10fba64c00012c579c00be7f8903a64e8004202c0900004d8200207d294214550800347c00406c39080020" +
    "7c0848404180fff44e800020010000000200000001800000030000000200000004000000018000000300000060000000";

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

  function stubBytes() {
    var out = new Uint8Array(STUB_HEX.length / 2);
    for (var i = 0; i < out.length; i++) out[i] = parseInt(STUB_HEX.substr(i * 2, 2), 16);
    return out;
  }

  function checkRange(name, address, size) {
    var end = address + size;
    if (address % 4 || size % 4)
      throw new Error(name + " (" + hex(address, 8) + ", " + size + " bytes) isn't in whole words");
    if (address < 0x80000000 || end > 0x81800000)
      throw new Error(name + " (" + hex(address, 8) + "-" + hex(end, 8) + ") is outside main memory " +
                      "(0x80000000-0x81800000)");
    if (address < LOW_END)
      throw new Error(name + " (" + hex(address, 8) + "-" + hex(end, 8) + ") reaches low memory, below " +
                      hex(LOW_END, 8) + ", which the loader stub fills");
    if (end > EXPECTED_ENTRY)
      throw new Error(name + " (" + hex(address, 8) + "-" + hex(end, 8) + ") reaches the payload image at " +
                      hex(EXPECTED_ENTRY, 8) + ", where the loader stub runs");
  }

  // An ordinary .dol as a .dol built for 0x81300000: the loader stub, a table and the sections (a
  // port of wrap() in tools/dolstub.py, with the same table and checks).
  function wrapDol(dol) {
    var view = new DataView(dol.buffer, dol.byteOffset, dol.byteLength);
    var w = function (i) { return view.getUint32(i * 4, false); };
    var found = [], n, i;
    for (n = 0; n < 18; n++) if (w(36 + n)) found.push([n, w(18 + n), w(n), w(36 + n)]);
    var bssAddress = w(54), bssSize = w(55), entry = w(56);
    for (i = 0; i < found.length; i++) {
      var name = "Section " + found[i][0];
      if (found[i][1] === 0) throw new Error(name + " has data but no load address");
      if (found[i][2] + found[i][3] > dol.length)
        throw new Error(name + " ends at " + hex(found[i][2] + found[i][3]) + ", past the end of the file (" +
                        hex(dol.length) + " bytes), file truncated?");
      checkRange(name, found[i][1], found[i][3]);
    }
    if (bssSize) checkRange("The BSS", bssAddress, bssSize);
    var inText = false;
    for (i = 0; i < found.length; i++)
      if (found[i][0] < 7 && found[i][1] <= entry && entry < found[i][1] + found[i][3]) inText = true;
    if (!inText) throw new Error("The entry point " + hex(entry, 8) + " isn't in a text section");
    var tableSize = 16 + 12 * found.length;
    tableSize += (32 - tableSize % 32) % 32;
    var stub = stubBytes(), dataSize = 0;
    for (i = 0; i < found.length; i++) dataSize += found[i][3];
    var image = new Uint8Array(stub.length + tableSize + dataSize), table = new DataView(image.buffer);
    image.set(stub);
    var t = stub.length, put = function (at, v) { table.setUint32(t + at, v, false); };
    put(0, entry); put(4, bssAddress); put(8, bssSize); put(12, found.length);
    var used = 0;
    for (i = 0; i < found.length; i++) {
      put(16 + 12 * i, found[i][1]); put(20 + 12 * i, tableSize + used); put(24 + 12 * i, found[i][3]);
      image.set(dol.subarray(found[i][2], found[i][2] + found[i][3]), t + tableSize + used);
      used += found[i][3];
    }
    var out = new Uint8Array(256 + image.length), head = new DataView(out.buffer);
    head.setUint32(0, 0x100, false); head.setUint32(18 * 4, EXPECTED_ENTRY, false);
    head.setUint32(36 * 4, image.length, false); head.setUint32(56 * 4, EXPECTED_ENTRY, false);
    out.set(image, 256);
    return out;
  }

  // Any .dol as a payload: an IPL build converts as it is, an ordinary one gets the loader stub in
  // front first. Returns {payload, loader}.
  function convertDol(dol) {
    var f = flattenDol(dol);
    if (((f[0] & 0x017FFFFF) >>> 0) === EXPECTED_BASE) return { payload: dolToPayload(dol), loader: false };
    return { payload: dolToPayload(wrapDol(dol)), loader: true };
  }

  var api = { dolToPayload: dolToPayload, convertDol: convertDol, flattenDol: flattenDol, scramble: scramble };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.PicoBootDol = api;
})(typeof window !== "undefined" ? window : this);
