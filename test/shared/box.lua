#!/usr/bin/env tarantool

local os   = require('os')
local fun  = require('fun')
local log  = require('log')
local json = require('json')

require('console').listen(os.getenv('ADMIN_PORT'))
box.cfg{
   listen           = os.getenv('PRIMARY_PORT'),
   log_level        = 5,
   logger           = 'tarantool.log',
   slab_alloc_arena = 0.2
}

box.once('init', function()
    box.schema.user.create('test', { password = 'test' })
    box.schema.user.create('test_empty', { password = '' })
    box.schema.user.create('test_big', {
        password = '123456789012345678901234567890123456789012345678901234567890'
    })
    box.schema.user.grant('test', 'read,write,execute', 'universe')

    local space = box.schema.space.create('test')
    space:create_index('primary', {
        type   = 'TREE',
        unique = true,
        parts  = {1, 'NUM'}
    })
    space:create_index('secondary', {
        type   = 'TREE',
        unique = false,
        parts  = {2, 'NUM', 3, 'STR'}
    })
    local space = box.schema.space.create('msgpack')
    space:create_index('primary', {
        parts = {1, 'NUM'}
    })
    space:insert{1, 'float as key', {
        [2.7] = {1, 2, 3}
    }}
    space:insert{2, 'array as key', {
        [{2, 7}] = {1, 2, 3}
    }}
    space:insert{3, 'array with float key as key', {
        [{
            [2.7] = 3,
            [7]   = 7
        }] = {1, 2, 3}
    }}
    space:insert{6, 'array with string key as key', {
        ['megusta'] = {1, 2, 3}
    }}
    local space = box.schema.space.create('pstring')
    space:create_index('primary', {
        parts = {1, 'STR'}
    })
end)

function test_1()
    return true, {
        c  = {
            ['106'] = {1, 1428578535},
            ['2']   = {1, 1428578535}
        },
        pc = {
            ['106'] = {1, 1428578535, 9243},
            ['2']   = {1, 1428578535, 9243}
        },
        s  = {1, 1428578535},
        u  = 1428578535,
        v  = {}
    }, true
end

function test_2()
    return {
        k2 = 'v',
        k1 = 'v2'
    }
end

function test_3(x, y)
    return x + y
end

function test_4(...)
    local args = {...}
    log.info('%s', json.encode(args))
    return 2
end

function test_5(count)
    log.info('duplicating %d arrays', count)
    local rv = fun.duplicate{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}:take(count):totable()
    log.info('%s', json.encode(rv))
    return rv
end

function test_6(...)
    return ...
end
