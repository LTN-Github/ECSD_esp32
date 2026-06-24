---
name: ir-control
description: Control air conditioner and other IR appliances via infrared remote codes.
---

# IR Remote Control

Control air conditioners, TVs, and other infrared appliances using learned remote codes.

## When to use

When the user asks to:
- Turn on/off the air conditioner
- Adjust AC temperature, mode, or fan speed
- Control any IR remote-controlled device
- Learn new remote control codes
- See what IR codes are available

## How to use

### Learning new codes (one-time setup)

1. Ask the user to point their remote at the IR receiver
2. Use `ir_receive` with a descriptive name
3. Ask the user to press the button on their remote
4. Repeat for each button

Example names for AC:
- `ac_power` - power on/off
- `ac_temp_up` - temperature up
- `ac_temp_down` - temperature down
- `ac_mode` - switch mode (cool/heat/dry/fan)
- `ac_fan` - fan speed
- `ac_swing` - swing on/off

### Sending codes

- Use `ir_send` with the code name: `ir_send {"name": "ac_power"}`
- For AC devices, send 2-3 times for reliability: `ir_send {"name": "ac_power", "repeat": 3}`

### Listing codes

- Use `ir_list` to see all stored codes

## Example conversations

User: "Turn on the air conditioner"
Action: `ir_send {"name": "ac_power", "repeat": 3}`
Response: "Air conditioner turned on."

User: "Turn off the AC"
Action: `ir_send {"name": "ac_power", "repeat": 3}`
Response: "Air conditioner turned off."

User: "Learn the AC remote"
Response: "Point your AC remote at the IR receiver. Press the power button now."
Action: `ir_receive {"name": "ac_power"}`
Response: "Got it! Now press the temperature up button."
Action: `ir_receive {"name": "ac_temp_up"}`
... continue for each button

## Notes

- IR codes are stored persistently on SPIFFS
- AC remotes often need `repeat: 2` or `repeat: 3` for reliable reception
- NEC protocol codes show address and command; raw codes are stored as-is
- Some AC brands use long protocols (48+ bits) - these are captured as raw and replayed exactly
