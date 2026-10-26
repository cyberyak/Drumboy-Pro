-- fractal.lua: audio-reactive fractal tree (show mode).
--
-- Enable with "fractal = on" in plugins.txt and restart. Then:
--   File menu -> press FILE (manager) -> select "fractal" -> press SELECT.
--   Press FILE to return to the normal screen.
--
-- Each branch level belongs to a track: level 1 (trunk) = track 1, level 2 = track 2, ...
-- A hit on a track lights its level in that track's colour and opens its branches.
-- The kick (track 1) and the bass of the mix make the tree grow, the treble twists it,
-- tracks 9 and 10 make it sway.
--
-- Knobs (while showing):
--   1 LEAN    tilt the tree            5 DETAIL      branch levels (4-9)
--   2 SIZE    trunk length             6 REACTION    how strongly hits open/light branches
--   3 SPREAD  how wide branches open   7 GLOW        how long a hit stays lit
--   4 SHAPE   shrink per level         8 COLOUR      rotate the track colours

local palette = {
  {255, 60, 60}, {255, 150, 40}, {255, 230, 60}, {120, 255, 80}, {40, 220, 180},
  {60, 170, 255}, {120, 100, 255}, {210, 90, 255}, {255, 90, 190}, {230, 230, 230},
}

-- Knob-controlled settings: value, min, max, step per click, label, display format
local knobs = {
  {name = "LEAN",     v = 0.0,  min = -0.6, max = 0.6,  step = 0.03, fmt = "%+.2f"},
  {name = "SIZE",     v = 0,    min = -40,  max = 40,   step = 2,    fmt = "%+d"},
  {name = "SPREAD",   v = 0.30, min = 0.05, max = 1.2,  step = 0.03, fmt = "%.2f"},
  {name = "SHAPE",    v = 0.68, min = 0.50, max = 0.80, step = 0.01, fmt = "%.2f"},
  {name = "DETAIL",   v = 8,    min = 4,    max = 9,    step = 1,    fmt = "%d"},
  {name = "REACTION", v = 1.0,  min = 0.0,  max = 2.0,  step = 0.1,  fmt = "%.1f"},
  {name = "GLOW",     v = 0.86, min = 0.50, max = 0.97, step = 0.01, fmt = "%.2f"},
  {name = "COLOUR",   v = 0,    min = 0,    max = 9,    step = 1,    fmt = "%d", wrap = true},
}
local function k(i) return knobs[i].v end

local energy = {}
for i = 1, 10 do energy[i] = 0 end

local W, H = 920, 310
local t = 0
local labelKnob, labelUntil, labelShown = nil, 0, false
local autoDepth, frameMs, fastFrames = 9, 0, 0   -- detail is lowered automatically if frames get slow

local sin, cos, min, max, pi = math.sin, math.cos, math.min, math.max, math.pi

-- Two parameter sets: the tree drawn last frame (erased in black) and the new one.
local function newParams()
  local p = { spread = {}, bright = {}, color = {} }
  for d = 1, 9 do p.spread[d], p.bright[d], p.color[d] = 0, 0, 1 end
  return p
end
local cur, prev = newParams(), newParams()
local hasPrev = false

function on_note(layer, velocity)
  local e = velocity / 127
  if e > energy[layer] then energy[layer] = e end
end

function on_encoder(knob, delta)  -- knobs reach this plugin only while it is showing
  local s = knobs[knob]
  if not s then return end
  local v = s.v + delta * s.step
  if s.wrap then
    local span = s.max - s.min + 1
    v = s.min + (v - s.min) % span
  else
    v = max(s.min, min(s.max, v))
  end
  s.v = v
  labelKnob, labelUntil = knob, sys.millis() + 1500
end

function on_show_start()
  W, H = screen.width(), screen.height()
  hasPrev = false
  autoDepth, fastFrames = 9, 0
  labelShown = false
end

-- Draws the whole tree for parameter set p, in its colours or in black (erase).
local p, colored
local function branch(x, y, angle, len, d)
  local x1, y1 = x + cos(angle) * len, y - sin(angle) * len
  if colored then
    local c, b = palette[p.color[d]], p.bright[d]
    screen.color(c[1] * b, c[2] * b, c[3] * b)
  end
  screen.line(x, y, x1, y1)
  if d < p.depth then
    local s = p.spread[d]
    branch(x1, y1, angle + s + p.twist, len * p.shrink, d + 1)
    branch(x1, y1, angle - s + p.twist, len * p.shrink, d + 1)
  end
end

local function drawTree(params, inColour)
  p, colored = params, inColour
  if not inColour then screen.color(0, 0, 0) end
  branch(W / 2, H - 2, pi / 2 + params.lean, params.len, 1)
end

local function drawLabel()
  local now = sys.millis()
  if labelKnob and now < labelUntil then
    local s = knobs[labelKnob]
    screen.color(255, 255, 255)
    screen.font(2)
    screen.text(W - 6, 4, string.format("KNOB %d  %s  " .. s.fmt, labelKnob, s.name, s.v), "right")
    screen.font(1)
    labelShown = true
  elseif labelShown then
    screen.clear(W - 260, 2, 258, 14)
    labelShown = false
  end
end

function on_show_frame()
  local started = sys.millis()
  W, H = screen.width(), screen.height()
  t = t + 0.05
  local bass, treble = audio.bass(), audio.treble()
  local react = k(6)

  -- 1. NEW PARAMETERS FROM KNOBS, TRACK ENERGIES AND THE MIX
  local shrink, depth = k(4), min(k(5), autoDepth)
  local reach = (1 - shrink ^ depth) / (1 - shrink)          -- total height in trunk lengths
  cur.shrink, cur.depth = shrink, depth
  cur.len = max(4, min(H * 0.95 / reach, H * 0.19 * (1 + react * (0.35 * energy[1] + 0.8 * bass)) + k(2)))
  cur.lean = k(1) + sin(t * 0.7) * (0.04 + 0.15 * react * energy[9])
  cur.twist = sin(t * 2.3) * (0.02 + react * (0.6 * treble + 0.12 * energy[10]))
  for d = 1, depth do
    local e = min(1, energy[d] * react)
    cur.spread[d] = k(3) + 0.45 * e
    cur.bright[d] = 0.25 + 0.75 * e
    cur.color[d] = (d - 1 + k(8)) % 10 + 1
  end

  -- 2. ERASE LAST TREE, DRAW NEW ONE (no full clear: less flicker)
  if hasPrev then drawTree(prev, false) end
  drawTree(cur, true)
  cur, prev = prev, cur
  hasPrev = true

  -- 3. LABELS + DECAY
  screen.color(110, 110, 110)
  local limited = (autoDepth < k(5)) and string.format("  DETAIL LIMITED TO %d", autoDepth) or ""
  screen.text(6, 4, string.format("FRACTAL   KNOBS 1-8   FILE EXIT   %3d MS%s   ", frameMs, limited))
  drawLabel()
  local glow = k(7)
  for i = 1, 10 do energy[i] = energy[i] * glow end

  -- 4. ADAPT DETAIL TO THE DEVICE'S SPEED (the frame limit is 50 ms)
  frameMs = sys.millis() - started
  if frameMs > 30 and autoDepth > 4 then
    autoDepth, fastFrames = autoDepth - 1, 0
  elseif frameMs < 12 and autoDepth < 9 then
    fastFrames = fastFrames + 1
    if fastFrames > 60 then autoDepth, fastFrames = autoDepth + 1, 0 end
  end
end
