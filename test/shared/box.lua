#!/usr/bin/env tarantool

os = require('os')
require('console').listen(os.getenv('ADMIN_PORT'))
box.cfg{
   listen           = os.getenv('PRIMARY_PORT'),
   log_level        = 5,
   logger           = 'tarantool.log',
   slab_alloc_arena = 0.2
}

lp = {
   test = 'test',
   test_empty = '',
   test_big = '123456789012345678901234567890123456789012345678901234567890' -- '1234567890' * 6
}

for k, v in pairs(lp) do
   if #box.space._user.index.name:select{k} == 0 then
      box.schema.user.create(k, { password = v })
   end
end

box.schema.user.grant('test', 'read,write,execute', 'universe', nil, {if_not_exists=true})

if not box.space.test then
   local test = box.schema.space.create('test')
   test:create_index('primary',   {type = 'TREE', unique = true, parts = {1, 'NUM'}})
   test:create_index('secondary', {type = 'TREE', unique = false, parts = {2, 'NUM', 3, 'STR'}})
end

if not box.space.msgpack then
   local msgpack = box.schema.space.create('msgpack')
   msgpack:create_index('primary', {parts = {1, 'NUM'}})
   box.schema.user.grant('test', 'read,write', 'space', 'msgpack')
   msgpack:insert{1, 'float as key', {[2.7] = {1, 2, 3}}}
   msgpack:insert{2, 'array as key', {[{2, 7}] = {1, 2, 3}}}
   msgpack:insert{3, 'array with float key as key', {[{[2.7] = 3, [7] = 7}] = {1, 2, 3}}}
   msgpack:insert{6, 'array with string key as key', {['megusta'] = {1, 2, 3}}}
end

function test_1()
    return true, {
        c= {
            ['106']= {1, 1428578535},
            ['2']= {1, 1428578535}
        },
        pc= {
            ['106']= {1, 1428578535, 9243},
            ['2']= {1, 1428578535, 9243}
        },
        s= {1, 1428578535},
        u= 1428578535,
        v= {}
    }, true
end

function test_2()
    return { k2= 'v', k1= 'v2'}
end

function test_3(x, y)
    return x + y
end

function test_4(...)
    local args = {...}
    require('log').info('%s', require('yaml').encode(args))
    return 2
end
