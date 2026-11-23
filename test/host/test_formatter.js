// Contract test for ttn/formatter.js against the wire format in main/shed_proto.h.
// The byte vectors are the ones test_shed_proto.c uses, so a format change that updates
// only one side fails one of the two suites.
//
//     node test/host/test_formatter.js

"use strict";
const fs = require("fs");
const path = require("path");
const assert = require("assert");

const src = fs.readFileSync(path.join(__dirname, "../../ttn/formatter.js"), "utf8");
const { decodeUplink, encodeDownlink, decodeDownlink } =
  new Function(src + "\nreturn { decodeUplink, encodeDownlink, decodeDownlink };")();

let run = 0, failed = 0;
function check(name, fn) {
  run++;
  try {
    fn();
  } catch (e) {
    failed++;
    console.log(`  FAIL  ${name}: ${e.message}`);
  }
}

console.log("formatter.js");

check("uplink 05 02 01 decodes to button and input 2 active, output 2 on, reason input", () => {
  assert.deepStrictEqual(decodeUplink({ fPort: 1, bytes: [0x05, 0x02, 1] }).data, {
    button: true, input1: false, input2: true, output1: false, output2: true, reason: "input",
  });
});

check("uplink reasons decode by number", () => {
  assert.strictEqual(decodeUplink({ fPort: 1, bytes: [0, 0, 0] }).data.reason, "heartbeat");
  assert.strictEqual(decodeUplink({ fPort: 1, bytes: [0, 0, 3] }).data.reason, "boot");
});

check("uplink of the wrong length is an error", () => {
  assert.ok(decodeUplink({ fPort: 1, bytes: [0, 0] }).errors);
});

check("pulse output 1 for 30 s encodes to 01 03 1e on port 2", () => {
  assert.deepStrictEqual(encodeDownlink({ data: { output: 1, action: "pulse", seconds: 30 } }),
                         { fPort: 2, bytes: [1, 3, 30] });
});

check("on output 2 encodes to 02 01", () => {
  assert.deepStrictEqual(encodeDownlink({ data: { output: 2, action: "on" } }),
                         { fPort: 2, bytes: [2, 1] });
});

check("pulse without seconds defaults to 1 s", () => {
  assert.deepStrictEqual(encodeDownlink({ data: { output: 1, action: "pulse" } }).bytes, [1, 3, 1]);
});

check("output 3, unknown action and a 0 s pulse are errors", () => {
  assert.ok(encodeDownlink({ data: { output: 3, action: "on" } }).errors);
  assert.ok(encodeDownlink({ data: { output: 1, action: "open" } }).errors);
  assert.ok(encodeDownlink({ data: { output: 1, action: "pulse", seconds: 0 } }).errors);
});

check("downlink 01 03 05 decodes back to a 5 s pulse", () => {
  assert.deepStrictEqual(decodeDownlink({ fPort: 2, bytes: [1, 3, 5] }).data,
                         { output: 1, action: "pulse", seconds: 5 });
});

console.log(`\n${run} checks, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
