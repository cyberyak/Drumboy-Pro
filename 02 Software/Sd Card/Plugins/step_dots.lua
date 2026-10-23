-- step_dots.lua: 16 step dots in the top-right corner; the current step is lit,
-- and it flashes when a note plays. Enable with "step_dots = on" in plugins.txt.

local X, Y, SIZE, GAP = 700, 16, 8, 15
local current, shown = -1, -2
local flashUntil = 0

function on_step(step)
  current = step % 16
end

function on_note(layer, velocity)
  flashUntil = sys.millis() + 60
end

function on_frame()
  local flash = sys.millis() < flashUntil
  if current == shown and not flash then return end   -- redraw only when something changed

  for i = 0, 15 do
    if i == current then
      if flash then screen.color(255, 255, 255) else screen.color(0, 220, 255) end
    elseif i % 4 == 0 then
      screen.color(90, 90, 90)
    else
      screen.color(40, 40, 40)
    end
    screen.rect(X + i * GAP, Y, SIZE, SIZE, true)
  end
  shown = flash and -2 or current
end
