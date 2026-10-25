dofile(os.getenv("NAV"))
local W = {}
memory.registerwrite(0x2005, function(a, sz, v) W[#W+1] = v end)
to_map()
play({{"up",60},{"none",30},{"left",8},{"up",30},{"none",30},{"left",8},{"up",30},{"none",30},{"right",16},{"up",30}})
idle(360); press({A=true}); idle(120)
shot("jam_start")
local ss = savestate.create(1); savestate.save(ss)
local f = io.open(out.."/jam_nes.txt","w")
local schemes = {
  {"hold_up", function(n) return {up=true} end},
  {"up_A4", function(n) return {up=true, A=(n%8<4)} end},
  {"up_A6", function(n) return {up=true, A=(n%12<6)} end},
  {"up_A10", function(n) return {up=true, A=(n%20<10)} end},
  {"A_only", function(n) return {A=true} end},
  {"LR2", function(n) return {left=(n%4<2), right=(n%4>=2)} end},
  {"LR4", function(n) return {left=(n%8<4), right=(n%8>=4)} end},
  {"LR8", function(n) return {left=(n%16<8), right=(n%16>=8)} end},
  {"up_LR3", function(n) return {up=true, left=(n%6<3), right=(n%6>=3)} end},
  {"B_only", function(n) return {B=true} end},
  {"up_B4", function(n) return {up=true, B=(n%8<4)} end},
}
for _,sc in ipairs(schemes) do
  savestate.load(ss)
  local sy = {}
  for n=0,899 do
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
