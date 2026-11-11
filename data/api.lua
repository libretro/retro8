local string, math = ...

function all(t)
  if t == nil then return function() end end
  local i, last = 1, nil
  return function()
    if t[i] == last then i = i + 1 end
    while t[i] == nil and i <= #t do i = i + 1 end
    last = t[i]
    return last
  end
end

function add(t, v, i)
  if t ~= nil then
    if i ~= nil then
      table.insert(t, i, v)
    else
      t[#t+1] = v
    end
    return v
  end
end

function foreach(c, f)
  if c ~= nil then
    for value in all(c) do
      f(value)
    end
  end
end


function mapdraw(...)
  map(...)
end

function count(t, v)
  if t ~= nil then
    if v == nil then
      return #t
    end
    local n = 0
    for i = 1, #t do
      if t[i] == v then
        n = n + 1
      end
    end
    return n
  end
end

function del(t, v)
  if t ~= nil then
    local found = false
    for i = 1, #t do
      if t[i] == v then
        found = true
      end
      if found then
        t[i] = t[i+1]
      end
    end
    if found then
      return v
    end
  end
end

function cocreate(f)
  return coroutine.create(f)
end

function yield(...)
  return coroutine.yield(...)
end

function coresume(f, ...)
  return coroutine.resume(f, ...)
end

function costatus(f)
  return coroutine.status(f)
end

function deli(t, i)
  if t ~= nil and #t > 0 then
    if i == nil then
      i = #t
    end
    if i >= 1 and i <= #t then
      return table.remove(t, i)
    end
  end
end

function split(s, sep, convert)
  local r = {}
  if s == nil then
    return r
  end
  s = tostring(s)
  if type(sep) ~= "string" and type(sep) ~= "number" then
    sep = ","
  end
  if convert == nil then
    convert = true
  end
  local function push(x)
    if convert then
      local v = tonumber(x)
      if v ~= nil then
        x = v
      end
    end
    r[#r+1] = x
  end
  if type(sep) == "number" then
    sep = math.max(1, math.floor(sep))
    for i = 1, #s, sep do
      push(string.sub(s, i, i + sep - 1))
    end
  elseif sep == "" then
    for i = 1, #s do
      push(string.sub(s, i, i))
    end
  else
    local start = 1
    while true do
      local a, b = string.find(s, sep, start, true)
      if a == nil then
        push(string.sub(s, start))
        break
      end
      push(string.sub(s, start, a - 1))
      start = b + 1
    end
  end
  return r
end

function chr(...)
  local t = {...}
  for i = 1, select("#", ...) do
    t[i] = string.char(math.floor(tonumber(t[i]) or 0) % 256)
  end
  return table.concat(t)
end

function ord(s, i, n)
  if type(s) ~= "string" then
    return nil
  end
  i = math.floor(tonumber(i) or 1)
  n = math.floor(tonumber(n) or 1)
  return string.byte(s, i, i + n - 1)
end

function pack(...)
  return table.pack(...)
end

function unpack(t, i, j)
  return table.unpack(t, i, j)
end

function inext(t, i)
  i = (i or 0) + 1
  local v = t[i]
  if v ~= nil then
    return i, v
  end
end

function stop()
end

function resume()
end

function folder()
end

function save()
end

function ls()
  return {}
end

function reboot()
  run()
end

function rrectfill(x, y, w, h, r, c)
  x, y, w, h = math.floor(x), math.floor(y), math.floor(w), math.floor(h)
  if w < 1 or h < 1 then return end
  r = math.max(0, math.min(math.floor(r or 0), math.floor((math.min(w, h) - 1) / 2)))
  local x0, y0, x1, y1 = x + r, y + r, x + w - 1 - r, y + h - 1 - r
  rectfill(x0, y, x1, y + h - 1, c)
  rectfill(x, y0, x + w - 1, y1, c)
  if r > 0 then
    circfill(x0, y0, r, c)
    circfill(x1, y0, r, c)
    circfill(x0, y1, r, c)
    circfill(x1, y1, r, c)
  end
end

function rrect(x, y, w, h, r, c)
  x, y, w, h = math.floor(x), math.floor(y), math.floor(w), math.floor(h)
  if w < 1 or h < 1 then return end
  r = math.max(0, math.min(math.floor(r or 0), math.floor((math.min(w, h) - 1) / 2)))
  local x0, y0, x1, y1 = x + r, y + r, x + w - 1 - r, y + h - 1 - r
  rectfill(x0, y, x1, y, c)
  rectfill(x0, y + h - 1, x1, y + h - 1)
  rectfill(x, y0, x, y1)
  rectfill(x + w - 1, y0, x + w - 1, y1)
  local dx, dy, e = r, 0, 1 - r
  while r > 0 and dx >= dy do
    pset(x1 + dx, y0 - dy) pset(x1 + dy, y0 - dx) pset(x0 - dx, y0 - dy) pset(x0 - dy, y0 - dx)
    pset(x1 + dx, y1 + dy) pset(x1 + dy, y1 + dx) pset(x0 - dx, y1 + dy) pset(x0 - dy, y1 + dx)
    dy = dy + 1
    if e < 0 then e = e + 2 * dy + 1 else dx = dx - 1 e = e + 2 * (dy - dx) + 1 end
  end
end

function ovalfill(x0, y0, x1, y1, c)
  local cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
  local rx, ry = math.abs(x1 - x0) / 2 + 0.5, math.abs(y1 - y0) / 2 + 0.5
  for y = math.floor(math.min(y0, y1)), math.floor(math.max(y0, y1)) do
    local dy = (y - cy) / ry
    if dy >= -1 and dy <= 1 then
      local w = rx * math.sqrt(1 - dy * dy)
      rectfill(cx - w + 0.5, y, cx + w - 0.5, y, c)
    end
  end
end

function oval(x0, y0, x1, y1, c)
  local cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
  local rx, ry = math.abs(x1 - x0) / 2 + 0.5, math.abs(y1 - y0) / 2 + 0.5
  for y = math.floor(math.min(y0, y1)), math.floor(math.max(y0, y1)) do
    local dy = (y - cy) / ry
    if dy >= -1 and dy <= 1 then
      local w = rx * math.sqrt(1 - dy * dy)
      pset(cx - w + 0.5, y, c)
      pset(cx + w - 0.5, y, c)
    end
  end
  for x = math.floor(math.min(x0, x1)), math.floor(math.max(x0, x1)) do
    local dx = (x - cx) / rx
    if dx >= -1 and dx <= 1 then
      local h = ry * math.sqrt(1 - dx * dx)
      pset(x, cy - h + 0.5, c)
      pset(x, cy + h - 0.5, c)
    end
  end
end

-- button and fill pattern glyphs are variables in PICO-8, in both P8SCII and UTF-8 form
for k, v in pairs({
  ["\x8b"] = 0, ["\x91"] = 1, ["\x94"] = 2, ["\x83"] = 3, ["\x8e"] = 4, ["\x97"] = 5,
  ["\xe2\xac\x85"] = 0, ["\xe2\x9e\xa1"] = 1, ["\xe2\xac\x86"] = 2, ["\xe2\xac\x87"] = 3,
  ["\xf0\x9f\x85\xbe"] = 4, ["\xe2\x9d\x8e"] = 5,
  ["\x80"] = 0x0000, ["\xe2\x96\x88"] = 0x0000,
  ["\x81"] = 0x5a5a.8, ["\xe2\x96\x92"] = 0x5a5a.8,
  ["\x82"] = 0x511f.8, ["\xf0\x9f\x90\xb1"] = 0x511f.8,
  ["\x84"] = 0x7d7d.8, ["\xe2\x96\x91"] = 0x7d7d.8,
  ["\x85"] = 0xb81d.8, ["\xe2\x9c\xbd"] = 0xb81d.8,
  ["\x86"] = 0xf99f.8, ["\xe2\x97\x8f"] = 0xf99f.8,
  ["\x87"] = 0x51bf.8, ["\xe2\x99\xa5"] = 0x51bf.8,
  ["\x88"] = 0xb5bf.8, ["\xe2\x98\x89"] = 0xb5bf.8,
  ["\x89"] = 0x999f.8, ["\xec\x9b\x83"] = 0x999f.8,
  ["\x8a"] = 0xb11f.8, ["\xe2\x8c\x82"] = 0xb11f.8,
  ["\x8c"] = 0xa0e0.8, ["\xf0\x9f\x98\x90"] = 0xa0e0.8,
  ["\x8d"] = 0x9b3f.8, ["\xe2\x99\xaa"] = 0x9b3f.8,
  ["\x8f"] = 0xb1bf.8, ["\xe2\x97\x86"] = 0xb1bf.8,
  ["\x90"] = 0xf5ff.8, ["\xe2\x80\xa6"] = 0xf5ff.8,
  ["\x92"] = 0xb15f.8, ["\xe2\x98\x85"] = 0xb15f.8,
  ["\x93"] = 0x1b1f.8, ["\xe2\xa7\x97"] = 0x1b1f.8,
  ["\x95"] = 0xf5bf.8, ["\xcb\x87"] = 0xf5bf.8,
  ["\x96"] = 0x7adf.8, ["\xe2\x88\xa7"] = 0x7adf.8,
  ["\x98"] = 0x0f0f.8, ["\xe2\x96\xa4"] = 0x0f0f.8,
  ["\x99"] = 0x5555.8, ["\xe2\x96\xa5"] = 0x5555.8,
}) do
  _ENV[k] = v
  _ENV[k .. "\xef\xb8\x8f"] = v
end

function cstore()
end

function run()
  reload()
  if _init then _init() end
end

function info()
end

_ENV["?"] = print

function reset()
  memset(0x5f00, 0, 0x80)
  pal()
  clip()
  color(6)
  cursor(0, 0)
  poke(0x5f54, 0, 0x60, 0x20, 0x80)
  line()
  tline(13)
end

local rawpairs = pairs
function pairs(t)
  if t == nil then return function() end end
  return rawpairs(t)
end

function _map_display()
end

function holdframe()
end

function serial()
  return 0
end
