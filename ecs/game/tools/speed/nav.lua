-- shared: from power-on to the map in practice mode
dofile(os.getenv("LIB"))
function to_map()
  idle(500); press({start=true}); idle(120)
  hold({down=true}, 90); hold({left=true}, 120); press({A=true}); idle(200)
end
function play(seq)
  for _,st in ipairs(seq) do
    for i=1,st[2] do
      local b = {}; for w in string.gmatch(st[1], "%a+") do if w ~= "none" then b[w] = true end end
      joypad.set(1,b); emu.frameadvance()
    end
  end
end
JAM = {{"up",60},{"none",30},{"left",8},{"up",30},{"none",30},{"left",8},{"up",30},{"none",30},{"right",16},{"up",30}}
