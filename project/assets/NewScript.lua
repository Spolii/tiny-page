-- self has: x, y, rotation, scale_x, scale_y, name
-- helpers: log("text"), key_down("Left")

-- Runs once when Play starts
function start(self)
end

-- Runs every frame while playing. dt = seconds since the last frame
function update(self, dt)
    self.rotation = self.rotation + 120 * dt
end