local root = arg[1] or "."
local function walk(dir, fn)
    local p = io.popen('dir /s /b "' .. dir:gsub("/", "\\") .. '" 2>nul')
    if not p then return end
    for line in p:lines() do fn(line) end
    p:close()
end
local bad = 0
local function check(path)
    local lower = path:lower()
    if lower:find("decrypted") or lower:find("plaintext") then
        bad = bad + 1
        print("unexpected " .. path)
    end
end
walk(root .. "/garrysmod", check)
walk(root .. "/examples", check)
assert(bad == 0, "plaintext protected artifacts present")
print("PASS disk protection")
