// Converts a .dol with src/web/dol.js: node dol_convert.js IN OUT
// Writes the payload to OUT and "loader" or "plain" to stdout (whether a loader stub was put in
// front); on an error prints its message to stderr and exits 1.
const fs = require("fs");
const path = require("path");
const { convertDol } = require(path.join(__dirname, "..", "..", "src", "web", "dol.js"));

try {
  const result = convertDol(new Uint8Array(fs.readFileSync(process.argv[2])));
  fs.writeFileSync(process.argv[3], result.payload);
  process.stdout.write(result.loader ? "loader" : "plain");
} catch (e) {
  process.stderr.write(e.message);
  process.exit(1);
}
