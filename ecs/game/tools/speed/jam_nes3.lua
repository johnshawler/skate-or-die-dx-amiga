dofile(os.getenv("NAV"))
local W = {}
memory.registerwrite(0x2005, function(a, sz, v) W[#W+1] = v end)
to_map()
play({{"up",60},{"none",30},{"left",8},{"up",30},{"none",30},{"left",8},{"up",30},{"none",30},{"right",16},{"up",30}})
idle(360); press({A=true}); idle(120)
shot("jam_start")
local ss = savestate.create(1); savestate.save(ss)
local f = io.open(out.."/jam_nes.txt","w")
local schemes = {{"idle", function(n) return {} end}, {"A_first_then_idle", function(n) return {A=(n<20)} end}}
for _,sc in ipairs(schemes) do
  savestate.load(ss)
  local sy = {}
  for n=0,1499 do
    joypad.set(1, sc[2](n))
    W = {}
    emu.frameadvance()
    -- first pair of the frame: x then y
    sy[#sy+1] = (W[2] or -1) .. ":" .. (W[1] or -1)
  end
  f:write(sc[1] .. " " .. table.concat(sy, " ") .. "\n")
  shot("jam_"..sc[1])
end
f:close(); emu.exit()
