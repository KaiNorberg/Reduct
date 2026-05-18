local function mandelbrot(width, height, max_iter)
    local output = {}
    for y = 0, height - 1 do
        local ci = (y * 2 / height) - 1
        local line = ""
        for x = 0, width - 1 do
            local cr = (x * 3.5 / width) - 2.5
            local zr, zi = 0, 0
            local iter = 0
            while zr * zr + zi * zi <= 4 and iter < max_iter do
                local tr = zr * zr - zi * zi + cr
                zi = 2 * zr * zi + ci
                zr = tr
                iter = iter + 1
            end
            if iter == max_iter then
                line = line .. "*"
            else
                line = line .. " "
            end
        end
        table.insert(output, line)
    end
    return table.concat(output, "\n") .. "\n"
end

local result = mandelbrot(80, 40, 10000)
io.write(result)
