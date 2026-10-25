out = os.getenv("OUT") or "/tmp"
emu.speedmode("turbo")
function idle(n) for i=1,n do emu.frameadvance() end end
function hold(b, n) for i=1,n do joypad.set(1, b); emu.frameadvance() end end
function press(b, n) hold(b, n or 6); idle(12) end
function shot(name) gui.savescreenshotas(out.."/"..name..".png"); emu.frameadvance() end
