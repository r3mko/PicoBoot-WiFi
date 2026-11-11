// Converts a .dol with src/web/dol.js: node dol_convert.js IN OUT
// Writes the payload to OUT; on an error prints its message to stderr and exits 1.
const fs = require("fs");
const path = require("path");
const { dolToPayload } = require(path.join(__dirname, "..", "..", "src", "web", "dol.js"));

try {
  fs.writeFileSync(process.argv[3], dolToPayload(new Uint8Array(fs.readFileSync(process.argv[2]))));
} catch (e) {
  process.stderr.write(e.message);
  process.exit(1);
}
