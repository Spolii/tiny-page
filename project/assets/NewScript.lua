public("speed", 40)
public("turn_speed", 90)
public("moving", true)

function update(self, dt)
    self.rotation = self.rotation + self.turn_speed * dt
    if self.moving then
        self.x = self.x + self.look_x * self.speed * dt
        self.y = self.y + self.look_y * self.speed * dt
    end
end