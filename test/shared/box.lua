#!/usr/bin/env tarantool

local os   = require('os')
local fio  = require('fio')
local fun  = require('fun')
local log  = require('log')
local json = require('json')
local yaml = require('yaml')

log.info(fio.cwd())
log.info("admin: %s, primary: %s", os.getenv('ADMIN_PORT'), os.getenv('PRIMARY_PORT'))

-- {{{ Compatibility layer between different tarantool versions

local function parse_tarantool_version(component)
    local pattern = '^(%d+).(%d+).(%d+)-(%d+)-g[0-9a-f]+$'
    return tonumber((select(component, _TARANTOOL:match(pattern))))
end

local _TARANTOOL_MAJOR = parse_tarantool_version(1)
local _TARANTOOL_MINOR = parse_tarantool_version(2)
local _TARANTOOL_PATCH = parse_tarantool_version(3)
local _TARANTOOL_REV = parse_tarantool_version(4)

local function tarantool_version_at_least(major, minor, patch, rev)
    if _TARANTOOL_MAJOR < major then return false end
    if _TARANTOOL_MAJOR > major then return true end

    if _TARANTOOL_MINOR < minor then return false end
    if _TARANTOOL_MINOR > minor then return true end

    if _TARANTOOL_PATCH < patch then return false end
    if _TARANTOOL_PATCH > patch then return true end

    if _TARANTOOL_REV < rev then return false end
    if _TARANTOOL_REV > rev then return true end

    return true
end

local compat = {}

if tarantool_version_at_least(1, 7, 3, 351) then
    compat.log = 'log'
    compat.memtx_memory = 'memtx_memory'
    compat.memtx_memory_transform = function(v)
        return v
    end
else
    compat.log = 'logger'
    compat.memtx_memory = 'slab_alloc_arena'
    compat.memtx_memory_transform = function(v)
        return v / 1024 ^ 3
    end
end

if tarantool_version_at_least(1, 7, 1, 147) then
    compat.unsigned = 'unsigned'
    compat.string = 'string'
else
    compat.unsigned = 'NUM'
    compat.string = 'STR'
end

-- }}}

box.cfg{
    listen                = os.getenv('PRIMARY_PORT'),
    log_level             = 5,
    [compat.log]          = 'tarantool.log',
    [compat.memtx_memory] = compat.memtx_memory_transform(400 * 1024 * 1024),
}

box.once('initialization', function()
    box.schema.user.create('test', { password = 'test' })
    box.schema.user.create('test_empty', { password = '' })
    box.schema.user.create('test_big', {
        password = '123456789012345678901234567890123456789012345678901234567890'
    })
    box.schema.user.grant('test', 'read,write,execute', 'universe')

    local space = box.schema.space.create('test', {
        format = {
            { type = compat.unsigned, name = 'field1', add_field = 1 },
            { type = compat.unsigned, name = 's1'                    },
            { type = compat.string,   name = 's2'                    },
        }
    })
    space:create_index('primary', {
        type   = 'TREE',
        unique = true,
        parts  = {1, compat.unsigned}
    })
    space:create_index('secondary', {
        type   = 'TREE',
        unique = false,
        parts  = {2, compat.unsigned, 3, compat.string}
    })

    local space = box.schema.space.create('msgpack')
    space:create_index('primary', {
        parts = {1, compat.unsigned}
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

    local test_hash = box.schema.space.create('test_hash')
    test_hash:create_index('primary', {
        type = 'HASH',
        unique = true
    })
    test_hash:insert{1, 'hash-loc'}
    test_hash:insert{2, 'hash-col'}
    test_hash:insert{3, 'hash-olc'}

    local space = box.schema.space.create('pstring')
    space:create_index('primary', {
        parts = {1, compat.string}
    })
    local yml = io.open(fio.pathjoin(fio.cwd(), "../test/shared/queue.yml")):read("*a")
    local tuple = yaml.decode(yml)[1]
    tuple[1] = "12345"
    box.space._schema:insert(tuple)

    -- gh-151: Support new _index system space format
    -- After tarantool-1.7.5-153-g1651fc9be the new _index format
    -- was introduced. When an index part uses is_nullable or
    -- collation parameter, then the new format will be used.
    if tarantool_version_at_least(1, 7, 6, 0) then
        local test_index_part_176 = box.schema.space.create(
            'test_index_part_176',
            {
                format = {
                    {type = compat.unsigned, name = 'f1'},
                    {type = compat.unsigned, name = 'f2', is_nullable = true},
                    {type = compat.string, name = 'f3'},
            }
        })

        test_index_part_176:create_index('primary', {
            parts = {{1, compat.unsigned}}
        })
        test_index_part_176:create_index('secondary', {
            parts = {
                {2, compat.unsigned, is_nullable = true},
                {3, compat.string, collation = 'unicode_ci'}
            }
        })
    end
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

iproto_connect_counter = 0
function iproto_connect_count()
    return iproto_connect_counter
end

box.session.on_connect(function()
    -- box.session.type() was introduced in 1.7.4-370-g0bce2472b.
    --
    -- We're interested in iproto sessions, but it is okay for our
    -- usage scenario to count replication and console sessions
    -- too: we only see to a delta and AFAIK our testing harness
    -- does not perform any background reconnections.
    if box.session.type == nil or box.session.type() == 'binary' then
        iproto_connect_counter = iproto_connect_counter + 1
    end
end)

-- export tarantool_version_at_least function
_G.tarantool_version_at_least = tarantool_version_at_least

require('console').listen(os.getenv('ADMIN_PORT'))

