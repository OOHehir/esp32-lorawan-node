// TTN payload formatter for esp32-lorawan-node. Paste into the device's (or application's)
// Payload formatters > Uplink and Downlink, type "Custom Javascript formatter".
// Wire format: main/shed_proto.h. Change both together.

var REASONS = ["heartbeat", "input", "output", "boot"];
var ACTIONS = ["off", "on", "toggle", "pulse"];

function decodeUplink(input) {
  if (input.fPort !== 1 || input.bytes.length !== 3) {
    return { errors: ["expected 3 bytes on port 1, got " + input.bytes.length + " on port " + input.fPort] };
  }
  var b = input.bytes;
  return {
    data: {
      button: (b[0] & 1) !== 0,
      input1: (b[0] & 2) !== 0,
      input2: (b[0] & 4) !== 0,
      output1: (b[1] & 1) !== 0,
      output2: (b[1] & 2) !== 0,
      reason: REASONS[b[2]] || "unknown"
    }
  };
}

// {"output": 1|2, "action": "off"|"on"|"toggle"|"pulse", "seconds": 1-255 (pulse only)}
function encodeDownlink(input) {
  var d = input.data;
  var action = ACTIONS.indexOf(d.action);
  if ((d.output !== 1 && d.output !== 2) || action < 0) {
    return { errors: ["need output 1 or 2 and action off, on, toggle or pulse"] };
  }
  var bytes = [d.output, action];
  if (d.action === "pulse") {
    var s = d.seconds === undefined ? 1 : d.seconds;
    if (s < 1 || s > 255) {
      return { errors: ["seconds must be 1-255"] };
    }
    bytes.push(s);
  }
  return { fPort: 2, bytes: bytes };
}

function decodeDownlink(input) {
  var b = input.bytes;
  if (input.fPort !== 2 || b.length < 2) {
    return { errors: ["not a shed command"] };
  }
  var data = { output: b[0], action: ACTIONS[b[1]] || "unknown" };
  if (b.length === 3) {
    data.seconds = b[2];
  }
  return { data: data };
}
