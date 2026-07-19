print("---------------------------------")
print("DragonOS Lua Scripting Test")
print("---------------------------------")
local sum = 0
for i=1,100 do
    sum = sum + i
end
print("Sum of 1 to 100 is: " .. sum)
print("Garbage size: " .. collectgarbage("count") .. " KB")
print("---------------------------------")
