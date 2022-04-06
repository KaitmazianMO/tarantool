local t = require('luatest')
local g = t.group()

g.test_deep_copy = function()
    local e1 = box.error.new({code = 1, reason = "1"})
    local e2 = box.error.new({code = 2, reason = "2"})
    local e3 = box.error.new({code = 3, reason = "3"})
    local e4 = box.error.new({code = 4, reason = "4"})
    e1:set_prev(e2)
    e2:set_prev(e3)
    e3:set_prev(e4)

    local copy1 = e1:deep_copy()
    -- Deep copy check
    t.assert_not_equals(copy1.prev.prev.prev, e4)
    t.assert_not_equals(copy1.prev.prev, e3)
    t.assert_not_equals(copy1.prev, e2)
    t.assert_not_equals(copy1, e1)
    -- The copy object must be independent of the source object
    e3:set_prev(nil)
    t.assert_not_equals(copy1.prev.prev.prev, nil)
    e2:set_prev(nil)
    t.assert_not_equals(copy1.prev.prev, nil)
    e1:set_prev(nil)
    t.assert_not_equals(copy1.prev, nil)
end
