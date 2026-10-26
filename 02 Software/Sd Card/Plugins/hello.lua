-- hello.lua: template showing every callback. Enable with "hello = on" in plugins.txt.
-- print() output appears in the top strip for a few seconds.

local steps, notes = 0, 0

function on_load()                      -- once, after the script is loaded at boot
  print("hello from Lua", _VERSION)
end

function on_step(step)                  -- every 16th note while playing (0, 1, 2, ...)
  steps = step
end

function on_note(layer, velocity)       -- a sound played: layer 1-10, velocity 0-127
  notes = notes + 1
end

function on_key(keypad, key, pressed)   -- keypad 0 = layer keys, 1 = left, 2 = right; key 0-15
  if pressed then print("key", keypad, key) end
end

function on_encoder(knob, delta)        -- knob 1-8, delta +1 / -1
  print("knob", knob, delta)
end

function on_midi_in(byte) end           -- every received MIDI byte (clock filtered out)
function on_midi_out(status, d1, d2) end -- every sent MIDI message (clock filtered out)

function on_frame()                     -- ~30 times per second; the only place screen.* works
  screen.color(255, 200, 0)
  screen.text(700, 16, string.format("STEP %3d  NOTES %5d", steps, notes))
end
