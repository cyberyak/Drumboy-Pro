-- fractal.lua: audio-reactive fractal tree (show mode).
--
-- Enable with "fractal = on" in plugins.txt and restart. Then:
--   File menu -> press FILE (manager) -> select "fractal" -> press ADD.
--   Press FILE to return to the normal screen.
--
-- Each branch level belongs to a track: level 1 (trunk) = track 1, level 2 = track 2, ...
-- A hit on a track lights its level in that track's colour and opens its branches.
-- The kick (track 1) and the bass of the mix make the tree grow, the treble twists it,
-- tracks 9 and 10 make it sway. Knob 1 leans the tree, knob 2 changes its size.

local DEPTH = 8            -- 2^8 - 1 = 255 branches
local SHRINK = 0.68        -- each level is 68% of the one below
local DECAY = 0.86         -- per-frame energy decay

local palette = {
  {255, 60, 60}, {255, 150, 40}, {255, 230, 60}, {120, 255, 80}, {40, 220, 180},
  {60, 170, 255}, {120, 100, 255}, {210, 90, 255}, {255, 90, 190}, {230, 230, 230},
}

local energy = {}
for i = 1, 10 do energy[i] = 0 end

local W, H = 920, 310
local knobLean, knobSize = 0, 0
local t = 0

local sin, cos, min, pi = math.sin, math.cos, math.min, math.pi

-- Two parameter sets: the tree drawn last frame (erased in black) and the new one.
local function newParams()
  local p = { spread = {}, bright = {} }
  for d = 1, DEPTH do p.spread[d], p.bright[d] = 0, 0 end
  return p
end
local cur, prev = newParams(), newParams()
local hasPrev = false

function on_note(layer, velocity)
  local e = velocity / 127
  if e > energy[layer] then energy[layer] = e end
end

function on_encoder(knob, delta)  -- knobs reach this plugin only while it is showing
  if knob == 1 then
    knobLean = math.max(-0.6, math.min(0.6, knobLean + delta * 0.03))
  elseif knob == 2 then
    knobSize = math.max(-40, math.min(40, knobSize + delta * 2))
  end
end

function on_show_start()
  W, H = screen.width(), screen.height()
  hasPrev = false
end

-- Draws the whole tree for parameter set p, in its colours or in black (erase).
local p, colored
local function branch(x, y, angle, len, d)
  local x1, y1 = x + cos(angle) * len, y - sin(angle) * len
  if colored then
    local c, k = palette[d], p.bright[d]
    screen.color(c[1] * k, c[2] * k, c[3] * k)
  end
  screen.line(x, y, x1, y1)
  if d < DEPTH then
    local s = p.spread[d]
    branch(x1, y1, angle + s + p.twist, len * SHRINK, d + 1)
    branch(x1, y1, angle - s + p.twist, len * SHRINK, d + 1)
  end
end

local function drawTree(params, inColour)
  p, colored = params, inColour
  if not inColour then screen.color(0, 0, 0) end
  branch(W / 2, H - 2, pi / 2 + params.lean, params.len, 1)
end

function on_show_frame()
  t = t + 0.05
  local bass, treble = audio.bass(), audio.treble()

  -- 1. NEW PARAMETERS FROM TRACK ENERGIES AND THE MIX
  cur.len = min(H * 0.30, H * 0.19 * (1 + 0.35 * energy[1] + 0.8 * bass) + knobSize)
  cur.lean = knobLean + sin(t * 0.7) * (0.04 + 0.15 * energy[9])
  cur.twist = sin(t * 2.3) * (0.02 + 0.6 * treble + 0.12 * energy[10])
  for d = 1, DEPTH do
    local e = energy[d]
    cur.spread[d] = 0.30 + 0.45 * e
    cur.bright[d] = 0.25 + 0.75 * e
  end

  -- 2. ERASE LAST TREE, DRAW NEW ONE (no full clear: less flicker)
  if hasPrev then drawTree(prev, false) end
  drawTree(cur, true)
  cur, prev = prev, cur
  hasPrev = true

  -- 3. LABEL + DECAY
  screen.color(110, 110, 110)
  screen.text(6, 4, "FRACTAL   KNOB 1 LEAN   KNOB 2 SIZE   FILE EXIT")
  for i = 1, 10 do energy[i] = energy[i] * DECAY end
end
