if not loadstring then loadstring = load end
if not unpack then unpack = table.unpack end
if not setfenv then
    function setfenv(fn, env)
        local index = 1
        while true do
            local name = debug.getupvalue(fn, index)
            if not name then break end
            if name == "_ENV" then
                debug.setupvalue(fn, index, env)
                break
            end
            index = index + 1
        end
        return fn
    end
end
