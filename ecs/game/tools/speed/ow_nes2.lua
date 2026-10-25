dofile(os.getenv("NAV"))
to_map()
idle(60)
local ss = savestate.create(1); savestate.save(ss)
local f = io.open(out.."/ow2.csv","w")
for _,d in ipairs({"up","down","left","right"}) do
  savestate.load(ss)
  local line = d
  for n=0,179 do
    local b = {}; b[d]=true
    joypad.set(1,b); emu.frameadvance()
    if n%10==9 then
      -- find skater sprite: print first 3 sprites x/y
      line = line .. string.format(" [%d/%d %d/%d]", memory.readbyte(0x203+4), memory.readbyte(0x200+4), memory.readbyte(0x203+8), memory.readbyte(0x200+8))
    end
  end
  f:write(line.."\n")
  shot("ow_"..d)
end
f:close(); emu.exit()
