dofile(os.getenv("NAV"))
local W = {}
memory.registerwrite(0x2005, function(a, sz, v) W[#W+1] = "5:"..v end)
memory.registerwrite(0x2006, function(a, sz, v) W[#W+1] = "6:"..v end)
memory.registerwrite(0x2000, function(a, sz, v) W[#W+1] = "0:"..v end)
to_map()
play({{"up",60},{"none",30},{"right",8},{"up",30},{"none",30},{"right",8},{"up",30},{"none",30},{"left",16},{"up",30}})
idle(360)
press({A=true})
local f = io.open(out.."/race_nes.csv","w")
f:write("frame,writes,spr0y,spr0x\n")
for n=0,1500 do
  joypad.set(1,{})
  W = {}
  emu.frameadvance()
  f:write(string.format("%d,%s,%d,%d\n", n, table.concat(W, " "), memory.readbyte(0x200), memory.readbyte(0x203)))
  if n == 300 or n == 900 then shot("race"..n) end
end
f:close(); emu.exit()
